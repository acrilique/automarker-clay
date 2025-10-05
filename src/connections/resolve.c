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
