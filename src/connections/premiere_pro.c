#include "premiere_pro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

void install_cep_extension(void) {
    char command[2048];

#ifdef _WIN32
    char exe_path[MAX_PATH];
    GetModuleFileName(NULL, exe_path, MAX_PATH);
    char *last_slash = strrchr(exe_path, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }
    snprintf(command, sizeof(command), "cmd.exe /c \"%s\\resources\\installers\\extension_installer_win.bat\"", exe_path);
#else
    char *base_path = SDL_GetBasePath();
    if (!base_path) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't get base path: %s", SDL_GetError());
        return;
    }
    snprintf(command, sizeof(command), "sh \"%s/resources/installers/extension_installer_mac.sh\"", base_path);
    SDL_free(base_path);
#endif

    printf("Running command: %s\n", command);
    int result = system(command);

    if (result != 0) {
        printf("Extension installation failed with code %d\n", result);
    } else {
        printf("Extension installation script finished.\n");
    }
}


static int send_jsx(const char *jsx_payload) {
    CURL *curl;
    CURLcode res;
    int success = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        char data[1024];
        snprintf(data, sizeof(data), "{\"to_eval\": \"%s\"}", jsx_payload);

        curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:3000");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            success = -1;
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    curl_global_cleanup();
    return success;
}

int premiere_pro_add_markers(const double *beats, int num_beats) {
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

    return send_jsx(jsx_payload);
}

int premiere_pro_clear_all_markers(void) {
    const char *jsx_payload =
        "var markers = app.project.activeSequence.markers;"
        "var current_marker = markers.getFirstMarker();"
        "while (markers.numMarkers > 0) {"
        "var to_delete = current_marker;"
        "current_marker = markers.getNextMarker(current_marker);"
        "markers.deleteMarker(to_delete);"
        "}";

    return send_jsx(jsx_payload);
}
