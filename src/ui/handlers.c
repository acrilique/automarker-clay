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

#include "handlers.h"
#include "components.h"
#include "../connections/premiere_pro.h"
#include "../connections/after_effects.h"
#include "../connections/resolve.h"
#include "../../libs/SDL_sound/include/SDL3_sound/SDL_sound.h"
#include "../../libs/tinyfiledialogs/tinyfiledialogs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

void handle_open_browser(const char* url) {
#ifdef _WIN32
    ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/open", "open", url, (char *)NULL);
        _exit(127);
    }
#else
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
        _exit(127);
    }
#endif
}

void handle_close_modal(Clay_ElementId elementId,
                        Clay_PointerData pointerData, intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    app_state->modal.visible = false;
  }
}

void handle_install_cep_extension(Clay_ElementId elementId,
                                  Clay_PointerData pointerData,
                                  intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    install_cep_extension(app_state->base_path, &app_state->cep_install_state);
  }
}

void handle_open_github_issues(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    (void)userData;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        handle_open_browser("https://github.com/acrilique/automarker-clay/issues");
    }
}

void handle_toggle_check_for_updates(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        app_state->updater_state->check_on_startup = !app_state->updater_state->check_on_startup;
        updater_save_config(app_state->updater_state);
    }
}

void handle_update_now(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        updater_start_download(app_state->updater_state, app_state->curl_manager, app_state->base_path);
    }
}

void handle_skip_version(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        strncpy(app_state->updater_state->last_ignored_version, app_state->updater_state->latest_version, sizeof(app_state->updater_state->last_ignored_version) - 1);
        app_state->updater_state->last_ignored_version[sizeof(app_state->updater_state->last_ignored_version) - 1] = '\0';
        updater_save_config(app_state->updater_state);
        app_state->updater_state->status = UPDATE_STATUS_IDLE;
        app_state->modal.visible = false;
    }
}

void handle_update_button(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
    (void)elementId;
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        AppState *app_state = (AppState *)userData;
        app_state->modal.visible = true;
        app_state->modal.render_content = render_update_modal_content;
    }
}

void handle_help(Clay_ElementId elementId, Clay_PointerData pointerData,
                 intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    
    // Reset CEP install status if it was a completed state
    int cep_status = SDL_GetAtomicInt(&app_state->cep_install_state.status);
    if (cep_status == CEP_INSTALL_SUCCESS || cep_status == CEP_INSTALL_ERROR) {
      SDL_SetAtomicInt(&app_state->cep_install_state.status, CEP_INSTALL_IDLE);
    }
    
    app_state->modal.visible = true;
    app_state->modal.render_content = render_help_modal_content;
  }
}

void handle_mark_in(Clay_ElementId elementId, Clay_PointerData pointerData,
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

void handle_mark_out(Clay_ElementId elementId, Clay_PointerData pointerData,
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

void handle_send_markers(Clay_ElementId elementId, Clay_PointerData pointerData,
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

      // Handle edge case: no markers in selection
      if (markers_in_selection_count == 0) {
        return;
      }

      double *beats_in_seconds =
          SDL_malloc(sizeof(double) * markers_in_selection_count);
      if (!beats_in_seconds) {
        return;
      }
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

      switch ((ConnectedApp)SDL_GetAtomicInt(&app_state->connected_app)) {
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
      SDL_free(beats_in_seconds);
    }
  }
}

void handle_remove_markers(Clay_ElementId elementId,
                           Clay_PointerData pointerData, intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    switch ((ConnectedApp)SDL_GetAtomicInt(&app_state->connected_app)) {
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

void handle_waveform_interaction(Clay_ElementId elementId,
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
  }
}

void handle_scrollbar_interaction(Clay_ElementId elementId,
                                  Clay_PointerData pointerData,
                                  intptr_t userData) {
  AppState *app_state = (AppState *)userData;
  Clay_ElementData scrollbar_element = Clay_GetElementData(elementId);
  if (!scrollbar_element.found) {
    return;
  }

  float scrollbar_width = app_state->waveform_bbox.width;
  if (scrollbar_width <= 0)
    return; // Avoid division by zero if not rendered yet

  float thumb_width = (scrollbar_width / app_state->waveform_view.zoom);
  float track_width = scrollbar_width - thumb_width;
  if (track_width <= 0)
    return; // No room to scroll

  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    if (app_state->is_hovering_scrollbar_thumb) {
      app_state->waveform_interaction_state = INTERACTION_DRAGGING_SCROLLBAR;
      app_state->scrollbar_drag_start_x = pointerData.position.x;
      app_state->scrollbar_drag_start_scroll = app_state->waveform_view.scroll;
    } else {
      // Clicked on track, jump scroll
      float click_x =
          pointerData.position.x - scrollbar_element.boundingBox.x;
      app_state->waveform_view.scroll =
          (click_x - thumb_width / 2) / track_width;
    }
  }

  // Clamp scroll to valid range [0, 1]
  if (app_state->waveform_view.scroll < 0.0f)
    app_state->waveform_view.scroll = 0.0f;
  if (app_state->waveform_view.scroll > 1.0f)
    app_state->waveform_view.scroll = 1.0f;
}

void handle_play_pause(Clay_ElementId elementId, Clay_PointerData pointerData,
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

void handle_file_selection(Clay_ElementId elementId,
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
    if (!filterPatterns) {
      return;
    }
    
    int patterns_allocated = 0;
    char **current_pattern = filterPatterns;
    for (const Sound_DecoderInfo **info = decoder_info; *info != NULL; info++) {
        for (const char **ext = (*info)->extensions; *ext != NULL; ext++) {
            // Allocate memory for the pattern string, e.g., "*.wav"
            *current_pattern = SDL_malloc(strlen(*ext) + 3);
            if (!*current_pattern) {
              // Cleanup previously allocated patterns
              for (int i = 0; i < patterns_allocated; i++) {
                SDL_free(filterPatterns[i]);
              }
              SDL_free(filterPatterns);
              return;
            }
            snprintf(*current_pattern, strlen(*ext) + 3, "*.%s", *ext);
            current_pattern++;
            patterns_allocated++;
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
