#include "updater.h"
#include "connections/curl_manager.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#define GITHUB_API_URL "https://api.github.com/repos/acrilique/automarker-clay/releases/latest"

typedef struct {
    UpdaterState *updater_state;
    CurlManager *curl_manager;
} UpdateCheckData;

static int parse_version(const char *version_str, int *major, int *minor, int *patch) {
    return sscanf(version_str, "v%d.%d.%d", major, minor, patch);
}

// Helper to find a string value in a JSON response without a full parser.
// This is fragile but avoids a new dependency.
static bool find_json_string_value(const char *json, const char *key, char *buffer, size_t buffer_size) {
    const char *key_ptr = strstr(json, key);
    if (!key_ptr) return false;

    const char *value_start = strchr(key_ptr, ':');
    if (!value_start) return false;

    value_start = strchr(value_start, '"');
    if (!value_start) return false;
    value_start++; // Move past the opening quote

    const char *value_end = strchr(value_start, '"');
    if (!value_end) return false;

    size_t length = value_end - value_start;
    if (length >= buffer_size) return false;

    strncpy(buffer, value_start, length);
    buffer[length] = '\0';
    return true;
}

static void update_check_callback(const char *response, bool success, void *userdata) {
    UpdateCheckData *data = (UpdateCheckData *)userdata;
    UpdaterState *updater = data->updater_state;

    if (!success) {
        snprintf(updater->error_message, sizeof(updater->error_message), "Failed to fetch release info.");
        updater->status = UPDATE_STATUS_ERROR;
        SDL_free(data);
        return;
    }

    char remote_version_str[32];
    if (!find_json_string_value(response, "\"tag_name\"", remote_version_str, sizeof(remote_version_str))) {
        snprintf(updater->error_message, sizeof(updater->error_message), "No tag_name in release info.");
        updater->status = UPDATE_STATUS_ERROR;
        SDL_free(data);
        return;
    }
    int remote_major, remote_minor, remote_patch;
    int current_major, current_minor, current_patch;

    if (parse_version(remote_version_str, &remote_major, &remote_minor, &remote_patch) == 3 &&
        parse_version(APP_VERSION, &current_major, &current_minor, &current_patch) == 3) {

        if (strcmp(remote_version_str, updater->last_ignored_version) != 0 &&
            (remote_major > current_major ||
            (remote_major == current_major && remote_minor > current_minor) ||
            (remote_major == current_major && remote_minor == current_minor && remote_patch > current_patch))) {

            strncpy(updater->latest_version, remote_version_str, sizeof(updater->latest_version) - 1);

            // Find the correct asset download URL
            const char *assets_ptr = strstr(response, "\"assets\"");
            if (assets_ptr) {
                char asset_name[128];
                char download_url[256];

#if defined(__APPLE__)
    #if defined(__aarch64__)
                const char* platform_str = "macos-arm64.dmg";
    #else
                const char* platform_str = "macos-x86_64.dmg";
    #endif
#elif defined(_WIN32)
                const char* platform_str = "windows-x64.zip";
#else
                const char* platform_str = NULL;
#endif
                if (platform_str) {
                    const char* current_asset = assets_ptr;
                    while((current_asset = strstr(current_asset, "\"name\"")) != NULL) {
                        if (find_json_string_value(current_asset, "\"name\"", asset_name, sizeof(asset_name)) &&
                            strstr(asset_name, platform_str)) {
                            if (find_json_string_value(current_asset, "\"browser_download_url\"", download_url, sizeof(download_url))) {
                                strncpy(updater->download_url, download_url, sizeof(updater->download_url) - 1);
                                updater->status = UPDATE_STATUS_AVAILABLE;
                                break;
                            }
                        }
                        current_asset++; // Move past the found "name" to avoid re-matching
                    }
                }
            }
        } else {
            updater->status = UPDATE_STATUS_IDLE;
        }
    } else {
        snprintf(updater->error_message, sizeof(updater->error_message), "Failed to parse version strings.");
        updater->status = UPDATE_STATUS_ERROR;
    }

    SDL_free(data);
}

void updater_check_for_updates(UpdaterState *updater) {
    if (updater->status == UPDATE_STATUS_CHECKING) {
        return;
    }
    updater->status = UPDATE_STATUS_CHECKING;

    UpdateCheckData *data = SDL_malloc(sizeof(UpdateCheckData));
    data->updater_state = updater;
    data->curl_manager = curl_manager_create(); // Create a temporary manager for this check

    curl_manager_perform_get(data->curl_manager, GITHUB_API_URL, update_check_callback, data);
}

UpdaterState* updater_create(const char* org, const char* app) {
    UpdaterState* updater = SDL_calloc(1, sizeof(UpdaterState));
    if (!updater) {
        return NULL;
    }

    updater->status = UPDATE_STATUS_IDLE;
    updater->config_path = SDL_GetPrefPath(org, app);
    if (!updater->config_path) {
        SDL_free(updater);
        return NULL;
    }

    updater_load_config(updater);
    return updater;
}

void updater_destroy(UpdaterState* updater) {
    if (updater) {
        SDL_free(updater->config_path);
        SDL_free(updater);
    }
}

void updater_load_config(UpdaterState* updater) {
    char config_file_path[1024];
    snprintf(config_file_path, sizeof(config_file_path), "%sconfig.json", updater->config_path);

    updater->last_ignored_version[0] = '\0'; // Default to empty string

    SDL_IOStream *file = SDL_IOFromFile(config_file_path, "r");
    if (file) {
        long size = SDL_GetIOSize(file);
        char *buffer = (char *)SDL_malloc(size + 1);
        SDL_ReadIO(file, buffer, size);
        buffer[size] = '\0';
        SDL_CloseIO(file);

        char value[32];
        if (find_json_string_value(buffer, "\"check_on_startup\"", value, sizeof(value))) {
            updater->check_on_startup = (strcmp(value, "true") == 0);
        } else {
            updater->check_on_startup = true; // Default
        }

        if (find_json_string_value(buffer, "\"last_ignored_version\"", value, sizeof(value))) {
            strncpy(updater->last_ignored_version, value, sizeof(updater->last_ignored_version) - 1);
        }

        SDL_free(buffer);
    } else {
        updater->check_on_startup = true; // Default
    }
}

void updater_save_config(UpdaterState* updater) {
    char config_file_path[1024];
    snprintf(config_file_path, sizeof(config_file_path), "%sconfig.json", updater->config_path);

    SDL_IOStream *file = SDL_IOFromFile(config_file_path, "w");
    if (file) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "{\n  \"check_on_startup\": %s,\n  \"last_ignored_version\": \"%s\"\n}\n",
                 updater->check_on_startup ? "true" : "false",
                 updater->last_ignored_version);
        SDL_WriteIO(file, buffer, strlen(buffer));
        SDL_CloseIO(file);
    }
}

static void run_updater_script(const char* script_path) {
#ifdef _WIN32
    char command[1024];
    snprintf(command, sizeof(command), "powershell.exe -ExecutionPolicy Bypass -File \"%s\"", script_path);
    // Using system() for simplicity, but CreateProcess would be more robust
    system(command);
#else
    char command[1024];
    snprintf(command, sizeof(command), "chmod +x \"%s\" && \"%s\"", script_path, script_path);
    system(command);
#endif
    SDL_Quit();
    exit(0);
}

static void on_update_download_complete(const char* downloaded_path, bool success, void* userdata) {
    UpdaterState* updater = (UpdaterState*)userdata;
    if (success) {
        updater->status = UPDATE_STATUS_IDLE;
        printf("Update downloaded to: %s\n", downloaded_path);

#ifdef _WIN32
        char script_path[1024];
        snprintf(script_path, sizeof(script_path), "%s\\update.ps1", SDL_GetPrefPath(NULL, NULL));
        SDL_IOStream *file = SDL_IOFromFile(script_path, "w");
        if (file) {
            char script_content[2048];
            char* base_path_escaped = SDL_GetPath(SDL_GetBasePath());
            for (char* p = base_path_escaped; *p; ++p) if (*p == '\\') *p = '/';

            snprintf(script_content, sizeof(script_content),
                "Start-Sleep -Seconds 2\n"
                "Stop-Process -Name \"automarker-c\" -Force -ErrorAction SilentlyContinue\n"
                "Expand-Archive -Path \"%s\" -DestinationPath \"%s\" -Force\n"
                "Start-Process \"%s/automarker-c.exe\"\n"
                "Remove-Item -Path \"%s\"\n"
                "Remove-Item -Path $MyInvocation.MyCommand.Path\n",
                downloaded_path, base_path_escaped, base_path_escaped, downloaded_path);
            SDL_WriteIO(file, script_content, strlen(script_content));
            SDL_CloseIO(file);
            SDL_free(base_path_escaped);
            run_updater_script(script_path);
        }
#else
        char script_path[1024];
        snprintf(script_path, sizeof(script_path), "%s/update.sh", SDL_GetPrefPath(NULL, NULL));
        SDL_IOStream *file = SDL_IOFromFile(script_path, "w");
        if (file) {
            char script_content[2048];
            char* app_path = SDL_GetPath(SDL_GetBasePath());
            // On macOS, base_path is inside the .app bundle (e.g., /path/to/automarker-c.app/Contents/Resources/)
            // We need to go up three levels to get the path to the .app bundle itself.
            char* p = strstr(app_path, "/Contents/Resources/");
            if (p) *p = '\0';

            snprintf(script_content, sizeof(script_content),
                "#!/bin/bash\n"
                "sleep 2\n"
                "hdiutil attach \"%s\" -mountpoint /Volumes/AutoMarkerUpdate\n"
                "rsync -a --delete /Volumes/AutoMarkerUpdate/automarker-c.app/ \"%s/\"\n"
                "hdiutil detach /Volumes/AutoMarkerUpdate\n"
                "open \"%s\"\n"
                "rm \"%s\"\n"
                "rm -- \"$0\"\n",
                downloaded_path, app_path, app_path, downloaded_path);
            SDL_WriteIO(file, script_content, strlen(script_content));
            SDL_CloseIO(file);
            SDL_free(app_path);
            run_updater_script(script_path);
        }
#endif
    } else {
        snprintf(updater->error_message, sizeof(updater->error_message), "Failed to download update.");
        updater->status = UPDATE_STATUS_ERROR;
    }
}

static void on_update_download_progress(double progress, void* userdata) {
    UpdaterState* updater = (UpdaterState*)userdata;
    updater->download_progress = progress;
}

void updater_start_download(UpdaterState* updater, struct CurlManager* curl_manager, const char* base_path) {
    if (updater->status != UPDATE_STATUS_AVAILABLE) {
        return;
    }

    updater->status = UPDATE_STATUS_DOWNLOADING;
    updater->download_progress = 0.0;

    char temp_path[1024];
#ifdef _WIN32
    snprintf(temp_path, sizeof(temp_path), "%s\\update.zip", SDL_GetPrefPath(NULL, NULL));
#else
    snprintf(temp_path, sizeof(temp_path), "%s/update.dmg", SDL_GetPrefPath(NULL, NULL));
#endif

    curl_manager_download_file(
        curl_manager,
        updater->download_url,
        temp_path,
        on_update_download_complete,
        on_update_download_progress,
        updater
    );
}
