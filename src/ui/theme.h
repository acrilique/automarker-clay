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

#ifndef UI_THEME_H
#define UI_THEME_H

#include <SDL3/SDL.h>
#include "../../libs/clay/clay.h"

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
static const Clay_Color COLOR_BUTTON_BG_HOVER = {160, 160, 160, 255};

// Waveform colors
static const Clay_Color COLOR_WAVEFORM_BG = {60, 60, 60, 255};
static const Clay_Color COLOR_WAVEFORM_LINE = {255, 255, 255, 255};
static const Clay_Color COLOR_WAVEFORM_BEAT = {255, 255, 0, 255};

#endif // UI_THEME_H
