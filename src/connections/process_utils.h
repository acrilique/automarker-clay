#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#include <stdbool.h>

bool is_process_running(const char *process_name);
bool is_process_running_from_list(const char * const *process_names, int num_names);

#ifdef _WIN32
char* get_after_effects_path(void);
#endif

#endif // PROCESS_UTILS_H
