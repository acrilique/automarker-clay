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
#include <SDL3_image/SDL_image.h>
#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>

#include "../libs/SDL_sound/include/SDL3_sound/SDL_sound.h"

#define CLAY_IMPLEMENTATION
#include "../libs/clay/clay.h"

#include "app_state.h"
#include "ui/layout.h"
#include "ui/handlers.h"
#include "ui/theme.h"
#include "connections/curl_manager.h"

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

static void HandleClayErrors(Clay_ErrorData errorData) {
  printf("%s", errorData.errorText.chars);
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
#ifdef __APPLE__
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
  void *clayMemoryBuffer = SDL_malloc(totalMemorySize);
  if (!clayMemoryBuffer) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate memory for Clay arena");
    return SDL_APP_FAILURE;
  }
  Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, clayMemoryBuffer);

  int width, height;
  SDL_GetWindowSize(state->window, &width, &height);
  Clay_Initialize(clayMemory, (Clay_Dimensions){(float)width, (float)height},
                  (Clay_ErrorHandler){HandleClayErrors, 0});
  Clay_SetMeasureTextFunction(SDL_MeasureText, state->rendererData.fonts);

  // Load Icons
#ifdef __APPLE__
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
#ifdef __APPLE__
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
#ifdef __APPLE__
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
#ifdef __APPLE__
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
#ifdef __APPLE__
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
#ifdef __APPLE__
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
#ifdef __APPLE__
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
#ifdef __APPLE__
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
  state->is_hovering_scrollbar_thumb = false;
  state->scrollbar_drag_start_x = 0.0f;
  state->scrollbar_drag_start_scroll = 0.0f;

  state->is_tooltip_visible = false;
  state->tooltip_text = "";
  state->tooltip_target_id = (Clay_ElementId){0};

  state->modal.visible = false;
  state->modal.render_content = NULL;

  SDL_SetAtomicInt(&state->connected_app, APP_NONE);
  SDL_SetAtomicInt(&state->should_stop_app_status_thread, 0);
  state->app_status_thread = SDL_CreateThread(check_app_status, "AppStatusThread", (void *)state);

  state->curl_manager = curl_manager_create();
  state->updater_state = updater_create();
  SDL_SetAtomicInt(&state->cep_install_state.status, CEP_INSTALL_IDLE);

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
  case SDL_EVENT_MOUSE_MOTION: {
    AppState *state = (AppState *)appstate;
    Clay_SetPointerState((Clay_Vector2){event->motion.x, event->motion.y},
                         event->motion.state & SDL_BUTTON_LMASK);

    if (state->waveform_interaction_state != INTERACTION_NONE) {
      // printf("[DEBUG] Mouse Motion with Interaction: %d\n", state->waveform_interaction_state); // Uncomment if too verbose
      if (state->waveform_interaction_state ==
          INTERACTION_DRAGGING_SCROLLBAR) {
        float scrollbar_width = state->waveform_bbox.width;
        if (scrollbar_width > 0) {
          float thumb_width =
              (scrollbar_width / state->waveform_view.zoom);
          float track_width = scrollbar_width - thumb_width;
          if (track_width > 0) {
            float delta_x =
                event->motion.x - state->scrollbar_drag_start_x;
            float delta_scroll = delta_x / track_width;
            state->waveform_view.scroll =
                state->scrollbar_drag_start_scroll + delta_scroll;

            // Clamp scroll
            if (state->waveform_view.scroll < 0.0f)
              state->waveform_view.scroll = 0.0f;
            if (state->waveform_view.scroll > 1.0f)
              state->waveform_view.scroll = 1.0f;
          }
        }
      } else {
        AudioState *audio_state = state->audio_state;
        float click_x = event->motion.x - state->waveform_bbox.x;
        float waveform_width = state->waveform_bbox.width;

        unsigned int visibleSamples =
            (unsigned int)(audio_state->sample->buffer_size /
                           sizeof(float) / state->waveform_view.zoom);
        unsigned int maxStartSample =
            (audio_state->sample->buffer_size / sizeof(float)) -
            visibleSamples;
        unsigned int startSample =
            (unsigned int)(state->waveform_view.scroll * maxStartSample);
        unsigned int clicked_sample =
            startSample +
            (unsigned int)((click_x / waveform_width) * visibleSamples);

        switch (state->waveform_interaction_state) {
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
          if (clicked_sample > state->selection_drag_start) {
            audio_state->selection_start = state->selection_drag_start;
            audio_state->selection_end = clicked_sample;
          } else {
            audio_state->selection_start = clicked_sample;
            audio_state->selection_end = state->selection_drag_start;
          }
          // Ensure the selection is never zero-width.
          if (audio_state->selection_start == audio_state->selection_end) {
              audio_state->selection_end++;
          }
          break;
        default:
          break;
        }
      }
    }
  } break;
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    AppState *state = (AppState *)appstate;
    if (event->button.button == SDL_BUTTON_LEFT) {
        state->waveform_interaction_state = INTERACTION_NONE;
    }
    
    // Explicitly update Clay state
    // We use the event position. For button state, we know THIS button is up.
    // However, Clay_SetPointerState checks if the *primary* pointer is down.
    // If Left button went up, pointerDown should be false.
    // We can rely on SDL_GetMouseState to give us the current state of buttons.
    float x, y;
    const Uint32 button_state = SDL_GetMouseState(&x, &y);
    // Note: SDL_GetMouseState might reflect the state *after* the event processing in some backends,
    // but just in case, we can mask out the button that just went up if needed.
    // Usually SDL_GetMouseState is consistent with the event stream.
    
    bool is_down = (button_state & SDL_BUTTON_LMASK) != 0;
    Clay_SetPointerState((Clay_Vector2){x, y}, is_down);
    break; 
  }
  case SDL_EVENT_MOUSE_BUTTON_DOWN: {
    AppState *state = (AppState *)appstate;
    float x, y;
    const Uint32 button_state = SDL_GetMouseState(&x, &y);
    Clay_SetPointerState((Clay_Vector2){x, y},
                         (button_state & SDL_BUTTON_LMASK) != 0);

    SDL_Keymod mod_state = SDL_GetModState();
    bool ctrl_pressed = mod_state & SDL_KMOD_CTRL;
    bool shift_pressed = mod_state & SDL_KMOD_SHIFT;

    if (event->button.button == SDL_BUTTON_RIGHT && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
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
    } else if (event->button.button == SDL_BUTTON_LEFT && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
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
  state->is_hovering_scrollbar_thumb = false;

  curl_manager_update(state->curl_manager);

  Clay_BeginLayout();

  build_ui(state);

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
    SDL_SetAtomicInt(&state->should_stop_app_status_thread, 1);
    SDL_WaitThread(state->app_status_thread, NULL);
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

    // Free icon surfaces
    if (state->file_icon) SDL_DestroySurface(state->file_icon);
    if (state->play_icon) SDL_DestroySurface(state->play_icon);
    if (state->send_icon) SDL_DestroySurface(state->send_icon);
    if (state->remove_icon) SDL_DestroySurface(state->remove_icon);
    if (state->help_icon) SDL_DestroySurface(state->help_icon);
    if (state->mark_in_icon) SDL_DestroySurface(state->mark_in_icon);
    if (state->mark_out_icon) SDL_DestroySurface(state->mark_out_icon);
    if (state->update_icon) SDL_DestroySurface(state->update_icon);

    SDL_free(state);
  }

  Sound_Quit();
  TTF_Quit();
  curl_global_cleanup();
}
