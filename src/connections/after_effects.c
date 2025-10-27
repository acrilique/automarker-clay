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

#include "after_effects.h"
#include "process_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static void run_jsx_script(const char *script_content) {
    char temp_path[1024];
    int fd = -1;

#ifdef _WIN32
    char temp_dir[MAX_PATH];
    DWORD ret = GetTempPath(MAX_PATH, temp_dir);
    if (ret > MAX_PATH || ret == 0) {
        return;
    }
    if (GetTempFileName(temp_dir, "jsx", 0, temp_path) == 0) {
        return;
    }
#else
    strcpy(temp_path, "/tmp/ae_script-XXXXXX");
    fd = mkstemp(temp_path);
    if (fd == -1) {
        return;
    }
#endif

    FILE *fp;
    if (fd != -1) {
        fp = fdopen(fd, "w");
    } else {
        fp = fopen(temp_path, "w");
    }

    if (fp == NULL) {
        if (fd != -1) close(fd);
        remove(temp_path);
        return;
    }
    fputs(script_content, fp);
    fclose(fp);

#ifdef _WIN32
    char* ae_path = get_after_effects_path();
    if (ae_path) {
        char command[2048];
        snprintf(command, sizeof(command), "\"%s\" -ro \"%s\"", ae_path, temp_path);
        system(command);
        free(ae_path);
    }
#else
    char as_path[1024];
    strcpy(as_path, "/tmp/ae_launcher-XXXXXX.applescript");
    int as_fd = mkstemp(as_path);

    if (as_fd != -1) {
        FILE *as_fp = fdopen(as_fd, "w");
        if (as_fp) {
            // This script tells AE to activate and then execute our JSX file.
            fprintf(as_fp, "tell application id \"com.adobe.AfterEffects.application\"\n");
            fprintf(as_fp, "    activate\n");
            fprintf(as_fp, "    DoScriptFile (POSIX file \"%s\")\n", temp_path);
            fprintf(as_fp, "end tell\n");
            fclose(as_fp);

            char command[2048];
            snprintf(command, sizeof(command), "osascript \"%s\"", as_path);
            system(command);
        }
        remove(as_path); // Clean up the AppleScript file.
    }
#endif

    remove(temp_path);
}

void after_effects_add_markers(const double *beats, int num_beats) {
    char jsx_payload[4096] = "";
    char buffer[256];

    strcat(jsx_payload, "var beats = [");
    for (int i = 0; i < num_beats; i++) {
        sprintf(buffer, "%.2f", beats[i]);
        strcat(jsx_payload, buffer);
        if (i < num_beats - 1) {
            strcat(jsx_payload, ",");
        }
    }
    strcat(jsx_payload, "];");

    strcat(jsx_payload,
           "var comp = app.project.activeItem;"
           "if (comp instanceof CompItem) {"
           "for (var i = 0; i < beats.length; i++) {"
           "var compMarker = new MarkerValue(String(i));"
           "comp.markerProperty.setValueAtTime(beats[i], compMarker);"
           "}"
           "}");

    run_jsx_script(jsx_payload);
}

void after_effects_clear_all_markers(void) {
    const char *jsx_payload =
        "var comp = app.project.activeItem;"
        "if (comp instanceof CompItem) {"
        "for (var i = comp.markerProperty.numKeys; i > 0; i--) {"
        "comp.markerProperty.removeKey(1);"
        "}"
        "}";

    run_jsx_script(jsx_payload);
}
