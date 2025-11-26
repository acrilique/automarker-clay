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

#include "process_utils.h"
#include "process_names.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <tlhelp32.h>
    #ifndef F_OK
        #define F_OK   0
    #endif
#else
    #include <dirent.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <libproc.h>
        #include <sys/proc_info.h>
    #endif
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
                        char *ae_path = SDL_malloc(1024);
                        snprintf(ae_path, 1024, "%s\\Support Files\\AfterFX.exe", location);
                        if (_access(ae_path, F_OK) == 0) {
                            if (install_location) SDL_free(install_location);
                            install_location = ae_path;
                        } else {
                            SDL_free(ae_path);
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
    #ifdef __APPLE__
        pid_t pids[2048];
        int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
        if (count <= 0) {
            return false;
        }

        for (int i = 0; i < count; i++) {
            if (pids[i] == 0) {
                continue;
            }
            char path[PROC_PIDPATHINFO_MAXSIZE];
            if (proc_pidpath(pids[i], path, sizeof(path)) > 0) {
                char *name = strrchr(path, '/');
                if (name != NULL) {
                    name++; // Move past the '/'
                    if (strcmp(name, process_name) == 0) {
                        return true;
                    }
                }
            }
        }
        return false;
    #else // Assuming Linux
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
#endif
}

bool is_process_running_from_list(const char * const *process_names, int num_names) {
#ifdef __APPLE__
    pid_t pids[2048];
    int count = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    if (count <= 0) {
        return false;
    }

    for (int i = 0; i < count; i++) {
        if (pids[i] == 0) {
            continue;
        }
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pids[i], path, sizeof(path)) > 0) {
            char *name = strrchr(path, '/');
            if (name != NULL) {
                name++; // Move past the '/'
                for (int j = 0; j < num_names; j++) {
                    if (strcmp(name, process_names[j]) == 0) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
#else
    for (int i = 0; i < num_names; i++) {
        if (is_process_running(process_names[i])) {
            return true;
        }
    }
    return false;
#endif
}
