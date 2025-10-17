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

#include "premiere_pro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <SDL3/SDL.h>

typedef struct {
    char *data;
    struct curl_slist *headers;
} JsxRequestData;

#ifdef _WIN32
#include <windows.h>
#endif

void install_cep_extension(const char *base_path) {
    char command[2048];
    char installer_path[1024];

#ifdef _WIN32
    snprintf(installer_path, sizeof(installer_path), "%sresources\\installers\\extension_installer_win.bat", base_path);
    snprintf(command, sizeof(command), "cmd.exe /c \"%s\"", installer_path);
#else
#ifdef __APPLE__
    snprintf(installer_path, sizeof(installer_path), "%s%s", base_path, "extension_installer_mac.sh");
#else
    snprintf(installer_path, sizeof(installer_path), "%sresources/installers/extension_installer_mac.sh", base_path);
#endif
    snprintf(command, sizeof(command), "sh \"%s\"", installer_path);
#endif

    printf("Running command: %s\n", command);
    int result = system(command);

    if (result != 0) {
        printf("Extension installation failed with code %d\n", result);
    } else {
        printf("Extension installation script finished.\n");
    }
}


static int send_jsx(CurlManager *curl_manager, const char *jsx_payload) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    JsxRequestData *request_data = malloc(sizeof(JsxRequestData));
    if (!request_data) {
        curl_easy_cleanup(curl);
        return -1;
    }

    request_data->headers = curl_slist_append(NULL, "Content-Type: application/json");
    request_data->data = malloc(4096);
    if (!request_data->data) {
        free(request_data);
        curl_easy_cleanup(curl);
        return -1;
    }
    snprintf(request_data->data, 4096, "{\"to_eval\": \"%s\"}", jsx_payload);

    curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:3000");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_data->headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_data->data);

    curl_manager_add_handle(curl_manager, curl, REQUEST_TYPE_JSX, request_data);

    return 0;
}

int premiere_pro_add_markers(CurlManager *curl_manager, const double *beats, int num_beats) {
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

    strcat(jsx_payload, "for (var i = 0; i < beats.length; i++) {");
    strcat(jsx_payload, "if (beats[i] < app.project.activeSequence.end) {");
    strcat(jsx_payload, "app.project.activeSequence.markers.createMarker(beats[i]);");
    strcat(jsx_payload, "}");
    strcat(jsx_payload, "}");

    return send_jsx(curl_manager, jsx_payload);
}

int premiere_pro_clear_all_markers(CurlManager *curl_manager) {
    const char *jsx_payload =
        "var markers = app.project.activeSequence.markers;"
        "var current_marker = markers.getFirstMarker();"
        "while (markers.numMarkers > 0) {"
        "var to_delete = current_marker;"
        "current_marker = markers.getNextMarker(current_marker);"
        "markers.deleteMarker(to_delete);"
        "}";

    return send_jsx(curl_manager, jsx_payload);
}
