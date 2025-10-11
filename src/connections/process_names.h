#ifndef PROCESS_NAMES_H
#define PROCESS_NAMES_H

#ifdef __APPLE__
static const char *PREMIERE_PROCESS_NAMES[] = {
    "Adobe Premiere Pro CS6",
    "Adobe Premiere Pro CC",
    "Adobe Premiere Pro CC 2014",
    "Adobe Premiere Pro CC 2015",
    "Adobe Premiere Pro CC 2017",
    "Adobe Premiere Pro CC 2018",
    "Adobe Premiere Pro 2019",
    "Adobe Premiere Pro 2020",
    "Adobe Premiere Pro 2021",
    "Adobe Premiere Pro 2022",
    "Adobe Premiere Pro 2023",
    "Adobe Premiere Pro 2024",
    "Adobe Premiere Pro 2025"
};
static const int NUM_PREMIERE_PROCESS_NAMES = sizeof(PREMIERE_PROCESS_NAMES) / sizeof(PREMIERE_PROCESS_NAMES[0]);

static const char *AFTERFX_PROCESS_NAMES[] = {
    "After Effects"
};
static const int NUM_AFTERFX_PROCESS_NAMES = sizeof(AFTERFX_PROCESS_NAMES) / sizeof(AFTERFX_PROCESS_NAMES[0]);

static const char *RESOLVE_PROCESS_NAMES[] = {
    "Resolve"
};
static const int NUM_RESOLVE_PROCESS_NAMES = sizeof(RESOLVE_PROCESS_NAMES) / sizeof(RESOLVE_PROCESS_NAMES[0]);

#else // For Windows and Linux
#define PREMIERE_PROCESS_NAME "adobe premiere pro.exe"
#define AFTERFX_PROCESS_NAME "AfterFX.exe"
#define RESOLVE_PROCESS_NAME "Resolve.exe"
#endif

#endif // PROCESS_NAMES_H
