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

#ifndef UI_HANDLERS_H
#define UI_HANDLERS_H

#include "../../libs/clay/clay.h"
#include "../app_state.h"

// Modal handlers
void handle_close_modal(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handle_install_cep_extension(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handle_open_github_issues(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handle_toggle_check_for_updates(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handle_update_now(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handle_skip_version(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handle_update_button(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);

// Button action handlers
void handleHelp(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handleMarkIn(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handleMarkOut(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void sendMarkers(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void removeMarkers(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handlePlayPause(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handleFileSelection(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);

// Waveform interaction handlers
void handleWaveformInteraction(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);
void handleScrollbarInteraction(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData);

// Utility function to open browser
void handle_open_browser(const char* url);

#endif // UI_HANDLERS_H
