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

#ifndef APP_STATE_H
#define APP_STATE_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include "../libs/clay/clay.h"
#include "audio_state.h"
#include "clay_renderer_SDL3.h"
#include "connections/curl_manager.h"
#include "connections/premiere_pro.h"
#include "updater.h"

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
  INTERACTION_DRAGGING_SELECTION,
  INTERACTION_DRAGGING_SCROLLBAR
} WaveformInteractionState;

// Forward declaration
typedef struct app_state AppState;

typedef struct {
  bool visible;
  void (*render_content)(AppState *app_state);
} ModalState;

struct app_state {
  SDL_Window *window;
  char *base_path;
  SDL_AtomicInt connected_app;
  SDL_AtomicInt should_stop_app_status_thread;
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
  bool is_hovering_scrollbar_thumb;
  float scrollbar_drag_start_x;
  float scrollbar_drag_start_scroll;

  // Tooltip state
  bool is_tooltip_visible;
  const char *tooltip_text;
  Clay_ElementId tooltip_target_id;

  // Modal state
  ModalState modal;

  WaveformData waveformData;
  CurlManager *curl_manager;
  UpdaterState *updater_state;
  CepInstallState cep_install_state;
};

// Helper function to get window width
int app_state_get_window_width(AppState *state);

// Thread function to check app status
int check_app_status(void *data);

#endif // APP_STATE_H
