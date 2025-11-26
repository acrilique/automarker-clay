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

#include "app_state.h"
#include "connections/process_utils.h"
#include "connections/process_names.h"

int app_state_get_window_width(AppState *state) {
  int w;
  SDL_GetWindowSize(state->window, &w, NULL);
  return w;
}

int check_app_status(void *data) {
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
