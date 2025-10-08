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

#ifdef _WIN32
char* get_after_effects_path(void) {
    HKEY hkey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        return NULL;
    }

    char subkey_name[255];
    DWORD subkey_name_size = sizeof(subkey_name);
    DWORD index = 0;
    char *install_location = NULL;

    while (RegEnumKeyEx(hkey, index, subkey_name, &subkey_name_size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        HKEY subkey;
        if (RegOpenKeyEx(hkey, subkey_name, 0, KEY_READ, &subkey) == ERROR_SUCCESS) {
            char display_name[255];
            DWORD display_name_size = sizeof(display_name);
            if (RegQueryValueEx(subkey, "DisplayName", NULL, NULL, (LPBYTE)display_name, &display_name_size) == ERROR_SUCCESS) {
                if (strstr(display_name, "Adobe After Effects") != NULL) {
                    char location[1024];
                    DWORD location_size = sizeof(location);
                    if (RegQueryValueEx(subkey, "InstallLocation", NULL, NULL, (LPBYTE)location, &location_size) == ERROR_SUCCESS) {
                        char *ae_path = malloc(1024);
                        snprintf(ae_path, 1024, "%s\\Support Files\\AfterFX.exe", location);
                        if (access(ae_path, F_OK) == 0) {
                            if (install_location) free(install_location);
                            install_location = ae_path;
                        } else {
                            free(ae_path);
                        }
                    }
                }
            }
            RegCloseKey(subkey);
        }
        subkey_name_size = sizeof(subkey_name);
        index++;
    }

    RegCloseKey(hkey);
    return install_location;
}
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
