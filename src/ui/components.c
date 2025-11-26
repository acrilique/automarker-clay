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

#include "components.h"
#include "theme.h"
#include "handlers.h"
#include "../updater.h"
#include <stdio.h>
#include <string.h>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

void headerButton(Clay_ElementId buttonId, Clay_ElementId iconId, SDL_Surface *icon,
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

void render_separator(void) {
    CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW()}, .padding = {.top = 2, .bottom = 2}}}) {
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIXED(2)}}, .backgroundColor = COLOR_BG_DARK});
    }
}

void render_cep_install_section(AppState *app_state) {
    int cep_status = SDL_GetAtomicInt(&app_state->cep_install_state.status);
    
    if (cep_status == CEP_INSTALL_IN_PROGRESS) {
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
            CLAY_TEXT(CLAY_STRING("Installing..."), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
        }
    } else {
        if (cep_status == CEP_INSTALL_SUCCESS) {
            CLAY_TEXT(CLAY_STRING("Extension installed successfully!"), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = {0, 255, 0, 255}}));
        } else if (cep_status == CEP_INSTALL_ERROR) {
            Clay_String error_msg = { .isStaticallyAllocated = true, .length = (int32_t)strlen(app_state->cep_install_state.error_message), .chars = app_state->cep_install_state.error_message };
            CLAY_TEXT(error_msg, CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = {255, 0, 0, 255}}));
        }
        
        CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
            Clay_OnHover(handle_install_cep_extension, (intptr_t)app_state);
            CLAY_TEXT(CLAY_STRING("Install CEP Extension"), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
        }
    }
}

void render_help_modal_content(AppState *app_state) {
    CLAY_TEXT(CLAY_STRING("Options"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
    
    // CEP Section
    CLAY_TEXT(CLAY_STRING("The CEP extension allows this app to communicate with Adobe Premiere Pro. If Premiere was running during the extension's installation, you'll need to restart it for the extension to be loaded."), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE}));
    
    render_cep_install_section(app_state);

    render_separator();

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

    render_separator();

    // Help/Issues Section
    CLAY_AUTO_ID({.layout = {.childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }}) {
      CLAY_TEXT(CLAY_STRING("Encountered a bug or need help?"), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE}));
      CLAY_AUTO_ID({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8)}, .backgroundColor = Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG, .cornerRadius = CLAY_CORNER_RADIUS(5)}) {
          Clay_OnHover(handle_open_github_issues, (intptr_t)app_state);
          CLAY_TEXT(CLAY_STRING("Get help"), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
      }
    }

    render_separator();

    static char version_text[128];
    snprintf(version_text, sizeof(version_text), "automarker %s by acrilique", APP_VERSION);
    Clay_String version_string = { .isStaticallyAllocated = true, .length = (int32_t)strlen(version_text), .chars = version_text };
    CLAY_TEXT(version_string, CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE, .textAlignment = CLAY_TEXT_ALIGN_CENTER}));
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
        static char update_text[256];
        snprintf(update_text, sizeof(update_text), "A new version (%s) is available. Do you want to update?", app_state->updater_state->latest_version);
        Clay_String update_string = { .isStaticallyAllocated = true, .length = (int32_t)strlen(update_text), .chars = update_text };  
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

void render_error_modal_content(AppState *app_state) {
    CLAY_TEXT(CLAY_STRING("Connection Error"), CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR, .textColor = COLOR_WHITE}));
    CLAY_TEXT(CLAY_STRING("Could not connect to Premiere Pro. Please make sure the extension is correctly installed."), CLAY_TEXT_CONFIG({.fontId = FONT_SMALL, .textColor = COLOR_WHITE}));
    render_cep_install_section(app_state);
}
