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

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <curl/curl.h>

#include <stdio.h>

#include "../libs/SDL_sound/include/SDL3_sound/SDL_sound.h"
#include "../libs/tinyfiledialogs/tinyfiledialogs.h"

#define CLAY_IMPLEMENTATION
#include "../libs/clay/clay.h"

#include "./clay_renderer_SDL3.c"
#include "audio_state.h"
#include "connections/process_utils.h"
#include "connections/process_names.h"
#include "connections/premiere_pro.h"
#include "connections/after_effects.h"
#include "connections/resolve.h"
#include "connections/curl_manager.h"
#include "updater.h"

// Font IDs
static const Uint32 FONT_REGULAR = 0;
static const Uint32 FONT_SMALL = 1;

// General colors
static const Clay_Color COLOR_BG_DARK = {43, 41, 51, 255};
static const Clay_Color COLOR_BG_LIGHT = {90, 90, 90, 255};
static const Clay_Color COLOR_WHITE = {255, 244, 244, 255};
static const Clay_Color COLOR_ACCENT = {140, 140, 140, 255};

// Button colors
static const Clay_Color COLOR_BUTTON_BG = {140, 140, 140, 255};
static const Clay_Color COLOR_BUTTON_BG_HOVER =
    {160, 160, 160, 255};

// Waveform colors
static const Clay_Color COLOR_WAVEFORM_BG = {60, 60, 60, 255};
static const Clay_Color COLOR_WAVEFORM_LINE = {255, 255, 255, 255};
static const Clay_Color COLOR_WAVEFORM_BEAT =
    {255, 255, 0, 255};

typedef struct {
  float zoom;   // Zoom level (1.0 = normal)
  float scroll; // Scroll position (0.0 = start, 1.0 = end)
} WaveformViewState;

typedef enum {
  APP_NONE,
  APP_PREMIERE,
  APP_AE,
  APP_RESOLVE
} ConnectedApp;

typedef enum {
  INTERACTION_NONE,
  INTERACTION_DRAGGING_PLAYHEAD,
  INTERACTION_DRAGGING_START_MARKER,
  INTERACTION_DRAGGING_END_MARKER,
  INTERACTION_DRAGGING_SELECTION
} WaveformInteractionState;

typedef struct app_state AppState;

typedef struct {
  bool visible;
  void (*render_content)(AppState *app_state);
} ModalState;

struct app_state {
  SDL_Window *window;
  char *base_path;
  ConnectedApp connected_app;
  SDL_Thread *app_status_thread;
  SDL_Surface *file_icon;
  SDL_Surface *play_icon;
  SDL_Surface *send_icon;
  SDL_Surface *remove_icon;
  SDL_Surface *help_icon;
  SDL_Surface *update_icon;
  SDL_Surface *mark_in_icon;
  SDL_Surface *mark_out_icon;

  Clay_SDL3RendererData rendererData;
  AudioState *audio_state;
  WaveformViewState waveform_view;
  struct {
    bool visible;
    int x, y;
  } context_menu;
  WaveformInteractionState waveform_interaction_state;
  bool is_hovering_selection_start;
  bool is_hovering_selection_end;
  bool is_selection_dragging;
  unsigned int selection_drag_start;
  Clay_BoundingBox waveform_bbox;

  // Tooltip state
  bool is_tooltip_visible;
  const char *tooltip_text;
  Clay_ElementId tooltip_target_id;

  // Modal state
  ModalState modal;

  WaveformData waveformData;
  CurlManager *curl_manager;
  UpdaterState *updater_state;
};


static int get_window_width(AppState *state) {
  int w;
  SDL_GetWindowSize(state->window, &w, NULL);
  return w;
}

static inline Clay_Dimensions SDL_MeasureText(Clay_StringSlice text,
                                              Clay_TextElementConfig *config,
                                              void *userData) {
  TTF_Font **fonts = userData;
  TTF_Font *font = fonts[config->fontId];
  int width, height;

  if (!TTF_GetStringSize(font, text.chars, text.length, &width, &height)) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to measure text: %s",
                 SDL_GetError());
  }

  return (Clay_Dimensions){(float)width, (float)height};
}

// Modal content rendering functions
static void handle_close_modal(Clay_ElementId elementId,
                           Clay_PointerData pointerData, intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    app_state->modal.visible = false;
  }
}

static void handle_install_cep_extension(Clay_ElementId elementId,
                                     Clay_PointerData pointerData,
                                     intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    install_cep_extension(app_state->base_path);
    app_state->modal.visible = false;
  }
}

static void handle_toggle_check_for_updates(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        app_state->updater_state->check_on_startup = !app_state->updater_state->check_on_startup;
        updater_save_config(app_state->updater_state);
    }
}

static void render_help_modal_content(AppState *app_state) {
    CLAY_TEXT(CLAY_STRING("Help"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
    CLAY_TEXT(CLAY_STRING("This is a placeholder for the help content."), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE}));

    // Auto-update checkbox
    CLAY_AUTO_ID({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}, .childGap = 8, .padding = CLAY_PADDING_ALL(4)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
        Clay_OnHover(handle_toggle_check_for_updates, (intptr_t)app_state);
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(20), .height = CLAY_SIZING_FIXED(20)}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}}, .border = {.color = COLOR_WHITE, .width = CLAY_BORDER_ALL(1)}, .backgroundColor = COLOR_BG_LIGHT}) {
            if (app_state->updater_state->check_on_startup) {
                CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(12), .height = CLAY_SIZING_FIXED(12)}}, .backgroundColor = COLOR_WHITE, .cornerRadius = CLAY_CORNER_RADIUS(2)});
            }
        }
        CLAY_TEXT(CLAY_STRING("Check for updates on startup"), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE}));
    }

    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
        Clay_OnHover(handle_install_cep_extension, (intptr_t)app_state);
        CLAY_TEXT(CLAY_STRING("Install CEP Extension"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
    }
}

static void handle_update_now(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        updater_start_download(app_state->updater_state, app_state->curl_manager, app_state->base_path);
        app_state->modal.visible = false;
    }
}

static void handle_skip_version(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        strncpy(app_state->updater_state->last_ignored_version, app_state->updater_state->latest_version, sizeof(app_state->updater_state->last_ignored_version) - 1);
        updater_save_config(app_state->updater_state);
        app_state->updater_state->status = UPDATE_STATUS_IDLE;
        app_state->modal.visible = false;
    }
}

void render_update_modal_content(AppState *app_state) {
    if (app_state->updater_state->status == UPDATE_STATUS_DOWNLOADING) {
        CLAY_TEXT(CLAY_STRING("Downloading Update..."), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
        
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(20)}}, .backgroundColor = COLOR_BG_DARK, .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
            CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_PERCENT(app_state->updater_state->download_progress), .height = CLAY_SIZING_GROW(0)}}, .backgroundColor = COLOR_ACCENT, .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
            }
        }
    } else {
        CLAY_TEXT(CLAY_STRING("Update Available"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
        char update_text[256];
        snprintf(update_text, sizeof(update_text), "A new version (%s) is available. Do you want to update?", app_state->updater_state->latest_version);
        Clay_String update_string = { .isStaticallyAllocated = false, .length = (int32_t)strlen(update_text), .chars = update_text };  
        CLAY_TEXT(update_string, CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE}));

        CLAY_AUTO_ID({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8, .sizing = {.width = CLAY_SIZING_GROW(0)}}}) {
            CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(1)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
                Clay_OnHover(handle_update_now, (intptr_t)app_state);
                CLAY_TEXT(CLAY_STRING("Update Now"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
            }
            CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(1)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
                Clay_OnHover(handle_skip_version, (intptr_t)app_state);
                CLAY_TEXT(CLAY_STRING("Skip Version"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
            }
            CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(1)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
                Clay_OnHover(handle_close_modal, (intptr_t)app_state);
                CLAY_TEXT(CLAY_STRING("Cancel"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
            }
        }
    }
}

static void render_error_modal_content(AppState *app_state) {
    CLAY_TEXT(CLAY_STRING("Connection Error"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
    CLAY_TEXT(CLAY_STRING("Could not connect to Premiere Pro. Please make sure it is running and the extension is installed."), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE}));
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
        Clay_OnHover(handle_install_cep_extension, (intptr_t)app_state);
        CLAY_TEXT(CLAY_STRING("Install CEP Extension"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
    }
}

static void handle_update_button(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        app_state->modal.visible = true;
        app_state->modal.render_content = render_update_modal_content;
    }
}

static void handleHelp(Clay_ElementId elementId, Clay_PointerData pointerData,
                        intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    app_state->modal.visible = true;
    app_state->modal.render_content = render_help_modal_content;
  }
}

static void
headerButton(Clay_ElementId buttonId, Clay_ElementId iconId, SDL_Surface *icon,
             const char *tooltip,
             void (*callback)(Clay_ElementId, Clay_PointerData, intptr_t),
             intptr_t userData) {

  CLAY(buttonId, {
      .backgroundColor =
          Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG,
      .layout = {.padding = CLAY_PADDING_ALL(4)},
      .cornerRadius = CLAY_CORNER_RADIUS(5),
  }) {
    if (callback) {
      Clay_OnHover(callback, userData);
    }

    if (Clay_Hovered()) {
      AppState *app_state = (AppState *)userData;
      app_state->is_tooltip_visible = true;
      app_state->tooltip_text = tooltip;
      app_state->tooltip_target_id = buttonId;
    }
    CLAY(iconId, {
          .layout = {.sizing = {.width = CLAY_SIZING_FIXED(50),
                                .height = CLAY_SIZING_GROW(0)}},
          .aspectRatio = {.aspectRatio = 1.0f},
          .image = {
              .imageData = icon,
          }});
  }
}

static void handleMarkIn(Clay_ElementId elementId, Clay_PointerData pointerData,
                        intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    AudioState *audio_state = app_state->audio_state;
    if (audio_state->status == STATUS_COMPLETED) {
      audio_state->selection_start = audio_state_get_playback_position(audio_state);
    }
  }
}

static void handleMarkOut(Clay_ElementId elementId, Clay_PointerData pointerData,
                         intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    AudioState *audio_state = app_state->audio_state;
    if (audio_state->status == STATUS_COMPLETED) {
      audio_state->selection_end = audio_state_get_playback_position(audio_state);
    }
  }
}

static void sendMarkers(Clay_ElementId elementId, Clay_PointerData pointerData,
                        intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    AudioState *audio_state = app_state->audio_state;

    if (audio_state->status == STATUS_COMPLETED) {
      int markers_in_selection_count = 0;
      for (int i = 0; i < audio_state->beat_count; i++) {
        if (audio_state->beat_positions[i] >= audio_state->selection_start &&
            audio_state->beat_positions[i] <= audio_state->selection_end) {
          markers_in_selection_count++;
        }
      }

      double *beats_in_seconds =
          malloc(sizeof(double) * markers_in_selection_count);
      int current_marker = 0;
      for (int i = 0; i < audio_state->beat_count; i++) {
        if (audio_state->beat_positions[i] >= audio_state->selection_start &&
            audio_state->beat_positions[i] <= audio_state->selection_end) {
          beats_in_seconds[current_marker] =
              (double)(audio_state->beat_positions[i] - audio_state->selection_start) /
                (audio_state->sample->actual.rate * audio_state->sample->actual.channels);
          current_marker++;
        }
      }

      switch (app_state->connected_app) {
      case APP_PREMIERE:
        if (premiere_pro_add_markers(app_state->curl_manager, beats_in_seconds, markers_in_selection_count) != 0) {
          app_state->modal.visible = true;
          app_state->modal.render_content = render_error_modal_content;
        }
        break;
      case APP_AE:
        after_effects_add_markers(beats_in_seconds, markers_in_selection_count);
        break;
      case APP_RESOLVE:
        resolve_add_markers(beats_in_seconds, markers_in_selection_count);
        break;
      default:
        break;
      }
      free(beats_in_seconds);
    }
  }
}

static void removeMarkers(Clay_ElementId elementId,
                         Clay_PointerData pointerData, intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    switch (app_state->connected_app) {
    case APP_PREMIERE:
      if (premiere_pro_clear_all_markers(app_state->curl_manager) != 0) {
        app_state->modal.visible = true;
        app_state->modal.render_content = render_error_modal_content;
      }
      break;
    case APP_AE:
      after_effects_clear_all_markers();
      break;
    case APP_RESOLVE:
      resolve_clear_all_markers();
      break;
    default:
      break;
    }
  }
}

static void handleWaveformInteraction(Clay_ElementId elementId,
                                     Clay_PointerData pointerData,
                                     intptr_t userData) {
  AppState *app_state = (AppState *)userData;
  AudioState *audio_state = app_state->audio_state;

  if (audio_state->status != STATUS_COMPLETED) {
    return;
  }

  Clay_ElementData waveform_element = Clay_GetElementData(elementId);
  if (!waveform_element.found) {
    return;
  }

  float click_x = pointerData.position.x - waveform_element.boundingBox.x;
  float waveform_width = waveform_element.boundingBox.width;

  unsigned int visibleSamples = (unsigned int)(audio_state->sample->buffer_size / sizeof(float) /
                               app_state->waveform_view.zoom);
  unsigned int maxStartSample =
      (audio_state->sample->buffer_size / sizeof(float)) - visibleSamples;
  unsigned int startSample = (unsigned int)(app_state->waveform_view.scroll * maxStartSample);

  // --- Hover detection ---
  const float hover_threshold = 5.0f; // 5 pixels tolerance

  // Calculate screen x for markers
  float start_marker_x = -1.0f;
  if (audio_state->selection_start > 0) {
    if (audio_state->selection_start >= startSample &&
        audio_state->selection_start < startSample + visibleSamples) {
      start_marker_x =
          ((float)(audio_state->selection_start - startSample) / visibleSamples) *
          waveform_width;
    }
  }

  float end_marker_x = -1.0f;
  if (audio_state->selection_end <
      (audio_state->sample->buffer_size / sizeof(float))) {
    if (audio_state->selection_end > startSample &&
        audio_state->selection_end <= startSample + visibleSamples) {
      end_marker_x =
          ((float)(audio_state->selection_end - startSample) / visibleSamples) *
          waveform_width;
    }
  }

  if (app_state->waveform_interaction_state == INTERACTION_NONE) {
    app_state->is_hovering_selection_start =
        (start_marker_x >= 0 && fabsf(click_x - start_marker_x) < hover_threshold);
    app_state->is_hovering_selection_end =
        (end_marker_x >= 0 && fabsf(click_x - end_marker_x) < hover_threshold);
  }

  // --- Interaction logic ---
  unsigned int clicked_sample =
      startSample + (unsigned int)((click_x / waveform_width) * visibleSamples);

  SDL_Keymod mod_state = SDL_GetModState();
  bool ctrl_pressed = mod_state & SDL_KMOD_CTRL;
  bool shift_pressed = mod_state & SDL_KMOD_SHIFT;

  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    if (ctrl_pressed && !shift_pressed) {
      app_state->waveform_interaction_state = INTERACTION_DRAGGING_SELECTION;
      app_state->selection_drag_start = clicked_sample;
      audio_state->selection_start = clicked_sample;
      audio_state->selection_end = clicked_sample;
    } else if (ctrl_pressed && shift_pressed) {
      audio_state->selection_start = clicked_sample;
    } else if (app_state->is_hovering_selection_start) {
      app_state->waveform_interaction_state = INTERACTION_DRAGGING_START_MARKER;
    } else if (app_state->is_hovering_selection_end) {
      app_state->waveform_interaction_state = INTERACTION_DRAGGING_END_MARKER;
    } else {
      app_state->waveform_interaction_state = INTERACTION_DRAGGING_PLAYHEAD;
      audio_state_set_playback_position(audio_state, clicked_sample);
    }
  } else if (pointerData.state == CLAY_POINTER_DATA_PRESSED) {
    switch (app_state->waveform_interaction_state) {
    case INTERACTION_DRAGGING_PLAYHEAD:
      audio_state_set_playback_position(audio_state, clicked_sample);
      break;
    case INTERACTION_DRAGGING_START_MARKER:
      if (clicked_sample < audio_state->selection_end) {
        audio_state->selection_start = clicked_sample;
      }
      break;
    case INTERACTION_DRAGGING_END_MARKER:
      if (clicked_sample > audio_state->selection_start) {
        audio_state->selection_end = clicked_sample;
      }
      break;
    case INTERACTION_DRAGGING_SELECTION:
      if (clicked_sample > app_state->selection_drag_start) {
        audio_state->selection_start = app_state->selection_drag_start;
        audio_state->selection_end = clicked_sample;
      } else {
        audio_state->selection_start = clicked_sample;
        audio_state->selection_end = app_state->selection_drag_start;
      }
      break;
    case INTERACTION_NONE:
      // Do nothing if not dragging
      break;
    }
  } else { // Not pressed
    app_state->waveform_interaction_state = INTERACTION_NONE;
  }
}

static void handlePlayPause(Clay_ElementId elementId, Clay_PointerData pointerData,
                            intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    AudioState *audio_state = app_state->audio_state;

    // Only allow play/pause if we have completed audio processing
    if (audio_state->status != STATUS_COMPLETED) {
      return;
    }

    switch (audio_state->playback_state) {
      case PLAYBACK_STOPPED:
        audio_state_start_playback(audio_state);
        break;
      case PLAYBACK_PLAYING:
        audio_state_pause_playback(audio_state);
        break;
      case PLAYBACK_PAUSED:
        audio_state_resume_playback(audio_state);
        break;
    }
  }
}

static void handleFileSelection(Clay_ElementId elementId,
                                Clay_PointerData pointerData,
                                intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    AudioState *audio_state = app_state->audio_state;

    // Stop any ongoing playback first
    audio_state_stop_playback(audio_state);

    // Dynamically get the list of supported file formats
    const Sound_DecoderInfo **decoder_info = Sound_AvailableDecoders();
    int num_patterns = 0;
    for (const Sound_DecoderInfo **info = decoder_info; *info != NULL; info++) {
        for (const char **ext = (*info)->extensions; *ext != NULL; ext++) {
            num_patterns++;
        }
    }

    char **filterPatterns = SDL_malloc(sizeof(char *) * (num_patterns + 1));
    char **current_pattern = filterPatterns;
    for (const Sound_DecoderInfo **info = decoder_info; *info != NULL; info++) {
        for (const char **ext = (*info)->extensions; *ext != NULL; ext++) {
            // Allocate memory for the pattern string, e.g., "*.wav"
            *current_pattern = SDL_malloc(strlen(*ext) + 3);
            snprintf(*current_pattern, strlen(*ext) + 3, "*.%s", *ext);
            current_pattern++;
        }
    }
    *current_pattern = NULL; // Null-terminate the list

    const char *selectedFile = tinyfd_openFileDialog(
        "Select Audio File", // title
        "",                  // default path
        num_patterns,        // number of filter patterns
        (const char *const *)filterPatterns, // filter patterns array
        "Audio Files",       // filter description
        0                    // allow multiple selections
    );

    // Free the allocated memory for the filter patterns
    for (int i = 0; i < num_patterns; i++) {
        SDL_free(filterPatterns[i]);
    }
    SDL_free(filterPatterns);

    // If no file was selected (dialog was cancelled), do nothing
    if (!selectedFile) {
      return;
    }

    // If a file was selected, check if we need to stop ongoing processing
    SDL_LockMutex(audio_state->data_mutex);
    bool is_processing = (audio_state->status == STATUS_DECODE ||
                         audio_state->status == STATUS_BEAT_ANALYSIS);
    SDL_UnlockMutex(audio_state->data_mutex);

    if (is_processing) {
      // Gracefully stop the ongoing processing
      audio_state_request_stop(audio_state);
    } else if (audio_state->processing_thread) {
      // Wait for any completed thread to finish
      SDL_WaitThread(audio_state->processing_thread, NULL);
      audio_state->processing_thread = NULL;
    }

    // Load the new file
    audio_state_load_file(audio_state, selectedFile);
  }
}

static void HandleClayErrors(Clay_ErrorData errorData) {
  printf("%s", errorData.errorText.chars);
}

static int check_app_status(void *data) {
    AppState *app_state = (AppState *)data;
    while (true) {
#ifdef __APPLE__
        if (is_process_running_from_list(PREMIERE_PROCESS_NAMES, NUM_PREMIERE_PROCESS_NAMES)) {
            app_state->connected_app = APP_PREMIERE;
        } else if (is_process_running_from_list(AFTERFX_PROCESS_NAMES, NUM_AFTERFX_PROCESS_NAMES)) {
            app_state->connected_app = APP_AE;
        } else if (is_process_running_from_list(RESOLVE_PROCESS_NAMES, NUM_RESOLVE_PROCESS_NAMES)) {
            app_state->connected_app = APP_RESOLVE;
        } else {
            app_state->connected_app = APP_NONE;
        }
#else
        if (is_process_running(PREMIERE_PROCESS_NAME)) {
            app_state->connected_app = APP_PREMIERE;
        } else if (is_process_running(AFTERFX_PROCESS_NAME)) {
            app_state->connected_app = APP_AE;
        } else if (is_process_running(RESOLVE_PROCESS_NAME)) {
            app_state->connected_app = APP_RESOLVE;
        } else {
            app_state->connected_app = APP_NONE;
        }
#endif
        SDL_Delay(1000); // Check every second
    }
    return 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (!TTF_Init()) {
    return SDL_APP_FAILURE;
  }

  if (!Sound_Init()) {
    return SDL_APP_FAILURE;
  }

  curl_global_init(CURL_GLOBAL_ALL);

  AppState *state = SDL_calloc(1, sizeof(AppState));
  if (!state) {
    return SDL_APP_FAILURE;
  }
  *appstate = state;

  if (!SDL_CreateWindowAndRenderer("automarker", 1000, 480, 0, &state->window,
                                   &state->rendererData.renderer)) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to create window and renderer: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_SetWindowResizable(state->window, true);
  SDL_SetWindowMinimumSize(state->window, 800, 480);

  state->rendererData.textEngine =
      TTF_CreateRendererTextEngine(state->rendererData.renderer);
  if (!state->rendererData.textEngine) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to create text engine from renderer: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state->rendererData.fonts = SDL_calloc(2, sizeof(TTF_Font *));
  if (!state->rendererData.fonts) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to allocate memory for the font array: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state->base_path = (char*)SDL_GetBasePath();
  if (!state->base_path) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't get base path: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }

  char resource_path[1024];

  // Load Fonts
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "Roboto-Regular.ttf");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/Roboto-Regular.ttf");
#endif
  TTF_Font *font_regular = TTF_OpenFont(resource_path, 22);
  if (!font_regular) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load regular font: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
  state->rendererData.fonts[FONT_REGULAR] = font_regular;

  TTF_Font *font_small = TTF_OpenFont(resource_path, 14);
  if (!font_small) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load small font: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
  state->rendererData.fonts[FONT_SMALL] = font_small;

  /* Initialize Clay */
  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));

  int width, height;
  SDL_GetWindowSize(state->window, &width, &height);
  Clay_Initialize(clayMemory, (Clay_Dimensions){(float)width, (float)height},
                  (Clay_ErrorHandler){HandleClayErrors, 0});
  Clay_SetMeasureTextFunction(SDL_MeasureText, state->rendererData.fonts);

  // Load Icons
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "file.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/file.svg");
#endif
  state->file_icon = IMG_Load(resource_path);
  if (!state->file_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load file icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "play_pause.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/play_pause.svg");
#endif
  state->play_icon = IMG_Load(resource_path);
  if (!state->play_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load play icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "send.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/send.svg");
#endif
  state->send_icon = IMG_Load(resource_path);
  if (!state->send_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load send icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "remove.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/remove.svg");
#endif
  state->remove_icon = IMG_Load(resource_path);
  if (!state->remove_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load remove icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "help.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/help.svg");
#endif
  state->help_icon = IMG_Load(resource_path);
  if (!state->help_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load help icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "mark_in.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/mark_in.svg");
#endif
  state->mark_in_icon = IMG_Load(resource_path);
  if (!state->mark_in_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load mark in icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "mark_out.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/mark_out.svg");
#endif
  state->mark_out_icon = IMG_Load(resource_path);
  if (!state->mark_out_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load mark out icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }
#ifdef MACOS_BUNDLE
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "update.svg");
#else
  snprintf(resource_path, sizeof(resource_path), "%s%s", state->base_path, "resources/update.svg");
#endif
  state->update_icon = IMG_Load(resource_path);
  if (!state->update_icon) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load update icon: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state->audio_state = audio_state_create();
  if (!state->audio_state) {
    SDL_free(state);
    return SDL_APP_FAILURE;
  }
  state->context_menu.visible = false;
  state->context_menu.x = 0;
  state->context_menu.y = 0;

  // Initialize waveform view state
  state->waveform_view.zoom = 50.0f;
  state->waveform_view.scroll = 0.0f;

  state->waveform_interaction_state = INTERACTION_NONE;
  state->is_hovering_selection_start = false;
  state->is_hovering_selection_end = false;
  state->is_selection_dragging = false;
  state->selection_drag_start = 0;
  state->waveform_bbox = (Clay_BoundingBox){0, 0, 0, 0};

  state->is_tooltip_visible = false;
  state->tooltip_text = "";
  state->tooltip_target_id = (Clay_ElementId){0};

  state->modal.visible = false;
  state->modal.render_content = NULL;

  state->connected_app = APP_NONE;
  state->app_status_thread = SDL_CreateThread(check_app_status, "AppStatusThread", (void *)state);

  state->curl_manager = curl_manager_create();
  state->updater_state = updater_create();

  if (state->updater_state->check_on_startup) {
      updater_check_for_updates(state->updater_state, state->curl_manager);
  }

  *appstate = state;
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  SDL_AppResult ret_val = SDL_APP_CONTINUE;

  switch (event->type) {
  case SDL_EVENT_QUIT:
    ret_val = SDL_APP_SUCCESS;
    break;
  case SDL_EVENT_WINDOW_RESIZED:
    Clay_SetLayoutDimensions((Clay_Dimensions){(float)event->window.data1,
                                               (float)event->window.data2});
    break;
  case SDL_EVENT_MOUSE_MOTION:
    Clay_SetPointerState((Clay_Vector2){event->motion.x, event->motion.y},
                         event->motion.state & SDL_BUTTON_LMASK);
    break;
  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_BUTTON_DOWN: {
    AppState *state = (AppState *)appstate;
    float x, y;
    const Uint32 button_state = SDL_GetMouseState(&x, &y);
    Clay_SetPointerState((Clay_Vector2){x, y},
                         (button_state & SDL_BUTTON_LMASK) != 0);

    SDL_Keymod mod_state = SDL_GetModState();
    bool ctrl_pressed = mod_state & SDL_KMOD_CTRL;
    bool shift_pressed = mod_state & SDL_KMOD_SHIFT;

    if (event->button.button == SDL_BUTTON_RIGHT && event->button.down) {
      if (ctrl_pressed && shift_pressed) {
        if (event->button.x >= state->waveform_bbox.x &&
            event->button.x <= state->waveform_bbox.x + state->waveform_bbox.width &&
            event->button.y >= state->waveform_bbox.y &&
            event->button.y <= state->waveform_bbox.y + state->waveform_bbox.height)
        {
            float click_x = event->button.x - state->waveform_bbox.x;
            float waveform_width = state->waveform_bbox.width;
            AudioState *audio_state = state->audio_state;

            unsigned int visibleSamples = (unsigned int)(audio_state->sample->buffer_size / sizeof(float) /
                                        state->waveform_view.zoom);
            unsigned int maxStartSample =
                (audio_state->sample->buffer_size / sizeof(float)) - visibleSamples;
            unsigned int startSample = (unsigned int)(state->waveform_view.scroll * maxStartSample);

            unsigned int clicked_sample =
                startSample + (unsigned int)((click_x / waveform_width) * visibleSamples);

            audio_state->selection_end = clicked_sample;
        }
      } else {
        state->context_menu.x = (int)event->button.x;
        state->context_menu.y = (int)event->button.y;
        state->context_menu.visible = true;
      }
    } else if (event->button.button == SDL_BUTTON_LEFT && event->button.down) {
      state->context_menu.visible = false;
    }
    break;
  }
  case SDL_EVENT_MOUSE_WHEEL: {
    AppState *state = (AppState *)appstate;

    // Handle vertical wheel for zoom (y)
    if (event->wheel.y != 0) {
      // Adjust zoom level - positive y means zoom in, negative means zoom out
      float zoom_delta = state->waveform_view.zoom * event->wheel.y /
                         10.0f; // Adjust sensitivity as needed
      state->waveform_view.zoom += zoom_delta;

      // Clamp zoom to reasonable values
      if (state->waveform_view.zoom < 1.0f)
        state->waveform_view.zoom = 1.0f;
      if (state->waveform_view.zoom > 1000.0f)
        state->waveform_view.zoom = 1000.0f;
    }

    // Handle horizontal wheel for scroll (x)
    if (event->wheel.x != 0) {
      float base_sensitivity = 0.10f;
      float zoom_factor = 1.0f / state->waveform_view.zoom;
      float scroll_delta = event->wheel.x * base_sensitivity * zoom_factor;

      state->waveform_view.scroll += scroll_delta;

      // Clamp scroll to valid range [0, 1]
      if (state->waveform_view.scroll < 0.0f)
        state->waveform_view.scroll = 0.0f;
      if (state->waveform_view.scroll > 1.0f)
        state->waveform_view.scroll = 1.0f;
    }

    // Also update Clay scroll containers
    Clay_UpdateScrollContainers(
        true, (Clay_Vector2){event->wheel.x, event->wheel.y}, 0.01f);
  } break;
  case SDL_EVENT_KEY_DOWN: {
    AppState *state = (AppState *)appstate;
    SDL_Keymod mod_state = SDL_GetModState();
    bool ctrl_pressed = mod_state & SDL_KMOD_CTRL;

    switch (event->key.key) {
    case SDLK_SPACE:
      handlePlayPause((Clay_ElementId){0}, (Clay_PointerData){.state = CLAY_POINTER_DATA_PRESSED_THIS_FRAME}, (intptr_t)state);
      break;
    case SDLK_F:
      if (ctrl_pressed) {
        handleFileSelection((Clay_ElementId){0}, (Clay_PointerData){.state = CLAY_POINTER_DATA_PRESSED_THIS_FRAME}, (intptr_t)state);
      }
      break;
    case SDLK_RETURN:
      if (ctrl_pressed) {
        sendMarkers((Clay_ElementId){0}, (Clay_PointerData){.state = CLAY_POINTER_DATA_PRESSED_THIS_FRAME}, (intptr_t)state);
      }
      break;
    case SDLK_BACKSPACE:
      if (ctrl_pressed) {
        removeMarkers((Clay_ElementId){0}, (Clay_PointerData){.state = CLAY_POINTER_DATA_PRESSED_THIS_FRAME}, (intptr_t)state);
      }
      break;
    }
  } break;
  default:
    break;
  };

  return ret_val;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  AppState *state = appstate;
  state->is_tooltip_visible = false;

  curl_manager_update(state->curl_manager);

  Clay_BeginLayout();

  Clay_Sizing layoutExpand = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)};

  // Build main UI layout
  CLAY(CLAY_ID("MainContainer"), {
        .backgroundColor = COLOR_BG_DARK,
        .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .sizing = layoutExpand,
                   .padding = CLAY_PADDING_ALL(16),
                   .childGap = 16}}) {

    // Modal
    if (state->modal.visible) {
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

    // Context menu
    if (state->context_menu.visible) {
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

    // Header bar
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

      if (state->updater_state->status == UPDATE_STATUS_AVAILABLE) {
        char tooltip[128];
        snprintf(tooltip, sizeof(tooltip), "Update to %s", state->updater_state->latest_version);
        headerButton(CLAY_ID("UpdateButton"), CLAY_ID("UpdateIcon"), state->update_icon,
                     tooltip, handle_update_button, (intptr_t)state);
      }

      // empty container to push status text to the right
      CLAY_AUTO_ID({.layout.sizing = {.width = CLAY_SIZING_GROW(1)}});

      switch (state->connected_app) {
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

    // Tooltip
    if (state->is_tooltip_visible) {
      Clay_ElementData target_element =
          Clay_GetElementData(state->tooltip_target_id);
      Clay_FloatingAttachPoints attach_points;

      if (target_element.found &&
          target_element.boundingBox.x < get_window_width(state) / 2) {
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

    // Main content area
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

      // Create custom element in the UI
      CLAY(CLAY_ID("WaveformDisplay"), {
        .backgroundColor = COLOR_WAVEFORM_BG,
        .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)}},
        .cornerRadius = CLAY_CORNER_RADIUS(8),
        .custom = {.customData = &state->waveformData},
      }) {
        Clay_OnHover(handleWaveformInteraction, (intptr_t)state);
      }
    }
  }

  Clay_RenderCommandArray render_commands = Clay_EndLayout();

  Clay_ElementData waveform_element = Clay_GetElementData(CLAY_ID("WaveformDisplay"));
  if (waveform_element.found) {
    state->waveform_bbox = waveform_element.boundingBox;
  }

  SDL_SetRenderDrawColor(state->rendererData.renderer, 0, 0, 0, 255);
  SDL_RenderClear(state->rendererData.renderer);

  SDL_Clay_RenderClayCommands(&state->rendererData, &render_commands);

  SDL_RenderPresent(state->rendererData.renderer);

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;

  if (result != SDL_APP_SUCCESS) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Application failed to run");
  }

  AppState *state = appstate;

  if (state) {
    SDL_DetachThread(state->app_status_thread);
    audio_state_destroy(state->audio_state);

    curl_manager_destroy(state->curl_manager);
    updater_destroy(state->updater_state);

    // Clean up SDL resources
    if (state->rendererData.renderer)
      SDL_DestroyRenderer(state->rendererData.renderer);

    if (state->window)
      SDL_DestroyWindow(state->window);

    if (state->rendererData.fonts) {
      TTF_CloseFont(state->rendererData.fonts[FONT_REGULAR]);
      TTF_CloseFont(state->rendererData.fonts[FONT_SMALL]);

      SDL_free(state->rendererData.fonts);
    }

    if (state->rendererData.textEngine)
      TTF_DestroyRendererTextEngine(state->rendererData.textEngine);

    SDL_free(state);
  }

  Sound_Quit();
  TTF_Quit();
  curl_global_cleanup();
}
