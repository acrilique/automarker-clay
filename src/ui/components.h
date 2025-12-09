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

#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include <SDL3/SDL.h>
#include "../../libs/clay/clay.h"
#include "../app_state.h"

// Header button component
void headerButton(Clay_ElementId buttonId, Clay_ElementId iconId, SDL_Surface *icon,
                  const char *tooltip,
                  void (*callback)(Clay_ElementId, Clay_PointerData, intptr_t),
                  intptr_t userData);

// Separator component
void render_separator(void);

// CEP install section (used in help and error modals)
void render_cep_install_section(AppState *app_state);

// Modal content renderers
void render_help_modal_content(AppState *app_state);
void render_update_modal_content(AppState *app_state);
void render_error_modal_content(AppState *app_state);

#endif // UI_COMPONENTS_H
