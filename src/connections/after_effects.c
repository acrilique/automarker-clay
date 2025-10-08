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
    // On macOS, we use osascript
    char command[2048];
    snprintf(command, sizeof(command), "osascript -e 'tell application \"Adobe After Effects\" to DoScriptFile \"%s\"'", temp_path);
    system(command);
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
