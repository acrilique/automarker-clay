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

typedef struct {
    char base_path[1024];
    CepInstallState *state;
} CepInstallData;

static int install_cep_thread(void *data) {
    CepInstallData *install_data = (CepInstallData *)data;
    CepInstallState *state = install_data->state;
    char command[2048];
    char installer_path[1024];
    const char *base_path = install_data->base_path;

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
        SDL_SetAtomicInt(&state->status, CEP_INSTALL_ERROR);
        snprintf(state->error_message, sizeof(state->error_message), "Installation failed (code %d)", result);
    } else {
        printf("Extension installation script finished.\n");
        SDL_SetAtomicInt(&state->status, CEP_INSTALL_SUCCESS);
    }

    SDL_free(install_data);
    return 0;
}

void install_cep_extension(const char *base_path, CepInstallState *state) {
    if (!state) {
        return;
    }

    if (!base_path) {
        SDL_SetAtomicInt(&state->status, CEP_INSTALL_ERROR);
        snprintf(state->error_message, sizeof(state->error_message), "base_path is NULL");
        return;
    }

    if (SDL_GetAtomicInt(&state->status) == CEP_INSTALL_IN_PROGRESS) {
        return;
    }

    size_t base_path_len = strlen(base_path);
    if (base_path_len >= sizeof(((CepInstallData *)0)->base_path)) {
        SDL_SetAtomicInt(&state->status, CEP_INSTALL_ERROR);
        snprintf(state->error_message, sizeof(state->error_message), "base_path too long (%zu bytes)", base_path_len);
        return;
    }

    CepInstallData *data = SDL_malloc(sizeof(CepInstallData));
    if (!data) {
        SDL_SetAtomicInt(&state->status, CEP_INSTALL_ERROR);
        snprintf(state->error_message, sizeof(state->error_message), "Memory allocation failed");
        return;
    }

    memcpy(data->base_path, base_path, base_path_len + 1);
    data->state = state;

    state->error_message[0] = '\0';
    SDL_SetAtomicInt(&state->status, CEP_INSTALL_IN_PROGRESS);
    SDL_Thread *thread = SDL_CreateThread(install_cep_thread, "CepInstallThread", data);
    
    if (!thread) {
        SDL_SetAtomicInt(&state->status, CEP_INSTALL_ERROR);
        snprintf(state->error_message, sizeof(state->error_message), "Failed to create thread: %s", SDL_GetError());
        SDL_free(data);
    } else {
        SDL_DetachThread(thread);
    }
}


static int send_jsx(CurlManager *curl_manager, const char *jsx_payload) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    JsxRequestData *request_data = SDL_malloc(sizeof(JsxRequestData));
    if (!request_data) {
        curl_easy_cleanup(curl);
        return -1;
    }

    request_data->headers = curl_slist_append(NULL, "Content-Type: application/json");
    request_data->data = SDL_malloc(4096);
    if (!request_data->data) {
        SDL_free(request_data);
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

typedef struct {
    void (*callback)(bool healthy, void *userdata);
    void *userdata;
} HealthCheckData;

static void health_check_callback(const char *response, bool success, void *userdata) {
    HealthCheckData *data = (HealthCheckData *)userdata;
    bool healthy = false;
    
    if (success && response) {
        // Check if response contains "Premiere is alive"
        healthy = (strstr(response, "Premiere is alive") != NULL);
    }
    
    if (data->callback) {
        data->callback(healthy, data->userdata);
    }
    
    SDL_free(data);
}

void premiere_pro_check_health(CurlManager *curl_manager, void (*callback)(bool healthy, void *userdata), void *userdata) {
    HealthCheckData *data = SDL_malloc(sizeof(HealthCheckData));
    if (!data) {
        if (callback) {
            callback(false, userdata);
        }
        return;
    }
    
    data->callback = callback;
    data->userdata = userdata;
    
    curl_manager_perform_get(curl_manager, "http://127.0.0.1:3000", health_check_callback, data);
}
