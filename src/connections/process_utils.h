#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#include <stdbool.h>

#ifdef _WIN32
#define PREMIERE_PROCESS_NAME "adobe premiere pro.exe"
#define AFTERFX_PROCESS_NAME "AfterFX.exe"
#define RESOLVE_PROCESS_NAME "Resolve.exe"
#else
#define PREMIERE_PROCESS_NAME "Adobe Premiere Pro"
#define AFTERFX_PROCESS_NAME "After Effects"
#define RESOLVE_PROCESS_NAME "Resolve"
#endif

bool is_process_running(const char *process_name);

#endif // PROCESS_UTILS_H
