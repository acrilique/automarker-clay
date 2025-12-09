/**
 * Copyright (C) 2025 Lluc Simó Margalef
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "layout.h"
#include "theme.h"
#include "components.h"
#include "handlers.h"
#include <stdio.h>
#include <string.h>

static void build_modal(AppState *state) {
  // Overlay
  CLAY_AUTO_ID({
    .backgroundColor = {0, 0, 0, 150},
    .layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}},
    .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.parent = CLAY_ATTACH_POINT_CENTER_CENTER, .element = CLAY_ATTACH_POINT_CENTER_CENTER}}
  }) {
    Clay_OnHover(handle_close_modal, (intptr_t)state);
  }

  // Modal container
  CLAY_AUTO_ID({
    .backgroundColor = COLOR_BG_LIGHT,
    .layout = {.padding = CLAY_PADDING_ALL(16), .childGap = 16, .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {.width = CLAY_SIZING_FIXED(400)}},
    .cornerRadius = CLAY_CORNER_RADIUS(8),
    .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.parent = CLAY_ATTACH_POINT_CENTER_CENTER, .element = CLAY_ATTACH_POINT_CENTER_CENTER}}
  }) {
    if (state->modal.render_content) {
      state->modal.render_content(state);
    }
  }
}

static void build_context_menu(AppState *state) {
  CLAY_AUTO_ID({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                      .attachPoints = {.parent = CLAY_ATTACH_POINT_LEFT_TOP},
                      .offset = {state->context_menu.x, state->context_menu.y}},
        .layout = {.padding = CLAY_PADDING_ALL(8)},
        .backgroundColor = COLOR_BG_LIGHT,
        .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
    CLAY_AUTO_ID({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = COLOR_BG_DARK,
          .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
      CLAY_AUTO_ID({.layout = {.padding = CLAY_PADDING_ALL(16)},
            .backgroundColor =
                Clay_Hovered() ? COLOR_ACCENT : COLOR_BG_DARK}) {
        CLAY_TEXT(CLAY_STRING("Option 1"),
                  CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR,
                                    .textColor = COLOR_WHITE}));
      }
    }
  }
}

static void build_header_bar(AppState *state) {
  CLAY(CLAY_ID("HeaderBar"), {
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0)},
                   .padding = CLAY_PADDING_ALL(16),
                   .childGap = 16,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = COLOR_BG_LIGHT,
        .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
    headerButton(CLAY_ID("FileButton"), CLAY_ID("FileIcon"), state->file_icon,
                 "Open audio file (Ctrl+F)", handleFileSelection,
                 (intptr_t)state);

    headerButton(CLAY_ID("PlayButton"), CLAY_ID("PlayIcon"), state->play_icon,
                 "Play/Pause (Space)", handlePlayPause, (intptr_t)state);

    headerButton(CLAY_ID("SendButton"), CLAY_ID("SendIcon"), state->send_icon,
                 "Send markers to connected app (Ctrl+Enter)", sendMarkers,
                 (intptr_t)state);

    headerButton(CLAY_ID("RemoveButton"), CLAY_ID("RemoveIcon"),
                 state->remove_icon, "Remove all markers from connected app (Ctrl+Backspace)",
                 removeMarkers, (intptr_t)state);

    headerButton(CLAY_ID("MarkInButton"), CLAY_ID("MarkInIcon"),
                 state->mark_in_icon, "Set selection start", handleMarkIn,
                 (intptr_t)state);

    headerButton(CLAY_ID("MarkOutButton"), CLAY_ID("MarkOutIcon"),
                 state->mark_out_icon, "Set selection end", handleMarkOut,
                 (intptr_t)state);

    headerButton(CLAY_ID("HelpButton"), CLAY_ID("HelpIcon"), state->help_icon,
                 "Help", handleHelp, (intptr_t)state);

    if (state->updater_state->status == UPDATE_STATUS_AVAILABLE || state->updater_state->status == UPDATE_STATUS_DOWNLOADING) {
      static char tooltip[128];
      if (state->updater_state->status == UPDATE_STATUS_DOWNLOADING) {
          snprintf(tooltip, sizeof(tooltip), "Downloading update... (%.0f%%)", state->updater_state->download_progress * 100);
      } else {
          snprintf(tooltip, sizeof(tooltip), "Update to %s", state->updater_state->latest_version);
      }
      headerButton(CLAY_ID("UpdateButton"), CLAY_ID("UpdateIcon"), state->update_icon,
                   tooltip, handle_update_button, (intptr_t)state);
    }

    // empty container to push status text to the right
    CLAY_AUTO_ID({.layout.sizing = {.width = CLAY_SIZING_GROW(1)}});

    switch ((ConnectedApp)SDL_GetAtomicInt(&state->connected_app)) {
    case APP_PREMIERE:
      CLAY_TEXT(CLAY_STRING("Premiere Pro Connected"),
                CLAY_TEXT_CONFIG(
                    {.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
      break;
    case APP_AE:
      CLAY_TEXT(CLAY_STRING("After Effects Connected"),
                CLAY_TEXT_CONFIG(
                    {.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
      break;
    case APP_RESOLVE:
      CLAY_TEXT(CLAY_STRING("DaVinci Resolve Connected"),
                CLAY_TEXT_CONFIG(
                    {.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
      break;
    default:
      CLAY_TEXT(CLAY_STRING("No App Connected"),
                CLAY_TEXT_CONFIG(
                    {.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
      break;
    }
  }
}

static void build_tooltip(AppState *state) {
  Clay_ElementData target_element =
      Clay_GetElementData(state->tooltip_target_id);
  Clay_FloatingAttachPoints attach_points;

  if (target_element.found &&
      target_element.boundingBox.x < app_state_get_window_width(state) / 2) {
    attach_points.parent = CLAY_ATTACH_POINT_LEFT_BOTTOM;
    attach_points.element = CLAY_ATTACH_POINT_LEFT_TOP;
  } else {
    attach_points.parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM;
    attach_points.element = CLAY_ATTACH_POINT_RIGHT_TOP;
  }

  CLAY(CLAY_ID("Tooltip"),
        {.floating = {.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                      .parentId = state->tooltip_target_id.id,
                      .attachPoints = attach_points,
                      .offset = {0, 8}},
        .layout = {.padding = CLAY_PADDING_ALL(4), .sizing = {.width = { .size = { .minMax = { .max = 280 }}}}},
        .backgroundColor = COLOR_BG_DARK,
        .border = {.color = COLOR_ACCENT,
                    .width = {.top = 1, .bottom = 1, .left = 1, .right = 1}},
        .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
    Clay_String tooltip_string = {.isStaticallyAllocated = true,
                                  .length = strlen(state->tooltip_text),
                                  .chars = state->tooltip_text};
    CLAY_TEXT(tooltip_string,
              CLAY_TEXT_CONFIG({.fontId = FONT_SMALL,
                                .textColor = COLOR_WHITE}));
  }
}

static void build_main_content(AppState *state) {
  Clay_Sizing layoutExpand = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)};

  CLAY(CLAY_ID("MainContent"), {
        .backgroundColor = COLOR_BG_LIGHT,
        .layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT,
                   .sizing = layoutExpand,
                   .padding = CLAY_PADDING_ALL(16),
                   .childGap = 16},
        .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
    // Create waveform data with current zoom and scroll values
    state->waveformData = (WaveformData){.samples = NULL,
                                 .sampleCount = 0,
                                 .beat_positions = NULL,
                                 .beat_count = 0,
                                 .currentZoom = state->waveform_view.zoom,
                                 .currentScroll = state->waveform_view.scroll,
                                 .lineColor = COLOR_WAVEFORM_LINE,
                                 .beatColor = COLOR_WAVEFORM_BEAT,
                                 .showPlaybackCursor = false,
                                 .playbackPosition = 0,
                                 .cursorColor = (Clay_Color){196, 94, 206, 255},
                                 .selection_start = 0,
                                 .selection_end = 0,
                                 .is_hovering_selection_start = state->is_hovering_selection_start,
                                 .is_hovering_selection_end = state->is_hovering_selection_end};

    // If we have audio data, use it
    SDL_LockMutex(state->audio_state->data_mutex);
    if (state->audio_state->status >= STATUS_BEAT_ANALYSIS &&
        state->audio_state->sample->buffer &&
        state->audio_state->sample->buffer_size > 0) {
      state->waveformData.samples = state->audio_state->sample->buffer;
      state->waveformData.sampleCount =
          state->audio_state->sample->buffer_size / sizeof(float);

      // Add beat positions if available
      if (state->audio_state->beat_positions &&
          state->audio_state->beat_count > 0) {
        state->waveformData.beat_positions = state->audio_state->beat_positions;
        state->waveformData.beat_count = state->audio_state->beat_count;

        // Debug info for beats
        static bool logged_beats = false;
        if (!logged_beats) {
          printf("Waveform display using %d beats\n",
                 state->waveformData.beat_count);
          logged_beats = true;
        }
      }

      // Add playback cursor if the track is loaded
      if (state->audio_state->status == STATUS_COMPLETED) {
        state->waveformData.showPlaybackCursor = true;
        state->waveformData.selection_start = state->audio_state->selection_start;
        state->waveformData.selection_end = state->audio_state->selection_end;

        unsigned int raw_pos =
            audio_state_get_playback_position(state->audio_state);

        // Compensate for audio buffer latency
        int latency_bytes = 0;
        if (state->audio_state->audio_stream) {
          latency_bytes =
              SDL_GetAudioStreamQueued(state->audio_state->audio_stream);
        }
        int latency_samples = latency_bytes / sizeof(float);

        int corrected_pos = raw_pos - latency_samples;
        if (corrected_pos < 0) {
          corrected_pos = 0;
        }

        state->waveformData.playbackPosition = (unsigned int)corrected_pos;
      }

      // Debug info
      static bool logged_waveform = false;
      if (!logged_waveform) {
        printf("Waveform display using %d samples\n",
               state->waveformData.sampleCount);
        logged_waveform = true;
      }
    }
    SDL_UnlockMutex(state->audio_state->data_mutex);

    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(1)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
      // Create custom element in the UI
      CLAY(CLAY_ID("WaveformDisplay"), {
        .backgroundColor = COLOR_WAVEFORM_BG,
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(1)}},
        .cornerRadius = CLAY_CORNER_RADIUS(8),
        .custom = {.customData = &state->waveformData},
      }) {
        Clay_OnHover(handleWaveformInteraction, (intptr_t)state);
      }

      // Scrollbar
      if (state->audio_state->status == STATUS_COMPLETED && state->waveform_view.zoom > 1.0f) {
          CLAY(CLAY_ID("Scrollbar"), {.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(12)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}, .backgroundColor = COLOR_WAVEFORM_BG, .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
              Clay_OnHover(handleScrollbarInteraction, (intptr_t)state);
              float scrollbar_width = state->waveform_bbox.width;
              if (scrollbar_width > 0) {
                  float thumb_width = (scrollbar_width / state->waveform_view.zoom);
                  float track_width = scrollbar_width - thumb_width;
                  float thumb_x = state->waveform_view.scroll * track_width;

                  CLAY(CLAY_ID("ScrollbarThumb"), {.layout = {.sizing = {.width = CLAY_SIZING_FIXED(thumb_width), .height = CLAY_SIZING_FIXED(10)}}, .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH, .attachPoints = {.parent = CLAY_ATTACH_POINT_LEFT_CENTER, .element = CLAY_ATTACH_POINT_LEFT_CENTER}, .offset = {thumb_x, 0}}}) {
                      state->is_hovering_scrollbar_thumb = Clay_PointerOver(CLAY_ID("ScrollbarThumb"));
                      CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = state->is_hovering_scrollbar_thumb ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)});
                  }
              }
          }
      }
    }
  }
}

void build_ui(AppState *state) {
  Clay_Sizing layoutExpand = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)};

  // Build main UI layout
  CLAY(CLAY_ID("MainContainer"), {
        .backgroundColor = COLOR_BG_DARK,
        .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .sizing = layoutExpand,
                   .padding = CLAY_PADDING_ALL(16),
                   .childGap = 16}}) {

    if (state->modal.visible) {
      build_modal(state);
    }
    
    if (state->context_menu.visible) {
      build_context_menu(state);
    }

    build_header_bar(state);

    if (state->is_tooltip_visible) {
      build_tooltip(state);
    }

    build_main_content(state);
  }
}
