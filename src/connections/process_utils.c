#include "process_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

bool is_process_running(const char *process_name) {
#ifdef _WIN32
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (Process32First(snapshot, &entry) == TRUE) {
        while (Process32Next(snapshot, &entry) == TRUE) {
            if (stricmp(entry.szExeFile, process_name) == 0) {
                CloseHandle(snapshot);
                return true;
            }
        }
    }

    CloseHandle(snapshot);
    return false;
#else
    DIR *dir = opendir("/proc");
    if (dir == NULL) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // If the entry is not a directory, or not a number, skip it
        if (!atoi(entry->d_name)) {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);

        FILE *fp = fopen(path, "r");
        if (fp == NULL) {
            continue;
        }

        char comm[256];
        if (fgets(comm, sizeof(comm), fp) != NULL) {
            // Remove newline character
            comm[strcspn(comm, "\n")] = 0;
            if (strcmp(comm, process_name) == 0) {
                fclose(fp);
                closedir(dir);
                return true;
            }
        }
        fclose(fp);
    }

    closedir(dir);
    return false;
#endif
}
