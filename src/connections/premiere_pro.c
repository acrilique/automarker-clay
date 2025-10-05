#include "premiere_pro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

static void send_jsx(const char *jsx_payload) {
    CURL *curl;
    CURLcode res;

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
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    curl_global_cleanup();
}

void premiere_pro_add_markers(const double *beats, int num_beats) {
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

    send_jsx(jsx_payload);
}

void premiere_pro_clear_all_markers(void) {
    const char *jsx_payload =
        "var markers = app.project.activeSequence.markers;"
        "var current_marker = markers.getFirstMarker();"
        "while (markers.numMarkers > 0) {"
        "var to_delete = current_marker;"
        "current_marker = markers.getNextMarker(current_marker);"
        "markers.deleteMarker(to_delete);"
        "}";

    send_jsx(jsx_payload);
}
