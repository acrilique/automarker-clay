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

#include "resolve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void run_resolve_script(const char *command, const double *beats, int num_beats) {
    char full_command[4096] = "python src/connections/resolve_helper.py ";
    strcat(full_command, command);

    if (beats != NULL) {
        for (int i = 0; i < num_beats; i++) {
            char beat_str[32];
            sprintf(beat_str, " %.2f", beats[i]);
            strcat(full_command, beat_str);
        }
    }

    system(full_command);
}

void resolve_add_markers(const double *beats, int num_beats) {
    run_resolve_script("add", beats, num_beats);
}

void resolve_clear_all_markers(void) {
    run_resolve_script("clear", NULL, 0);
}
