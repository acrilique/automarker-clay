#include "updater.h"
#include "connections/curl_manager.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#define GITHUB_API_URL "https://api.github.com/repos/acrilique/automarker-clay/releases/latest"

typedef struct {
    UpdaterState *updater_state;
} UpdateCheckData;

typedef struct {
    UpdaterState *updater;
    const char* base_path;
} DownloadCallbackData;

static int parse_version(const char *version_str, int *major, int *minor, int *patch) {
    if (sscanf(version_str, "v%d.%d.%d", major, minor, patch) == 3) {
        return 3;
    }
    return sscanf(version_str, "%d.%d.%d", major, minor, patch);
}

static void update_check_callback(const char *response, bool success, void *userdata) {
    UpdateCheckData *data = (UpdateCheckData *)userdata;
    UpdaterState *updater = data->updater_state;
    cJSON *json = NULL;

    if (!success) {
        snprintf(updater->error_message, sizeof(updater->error_message), "Failed to fetch release info.");
        updater->status = UPDATE_STATUS_ERROR;
        goto cleanup;
    }

    json = cJSON_Parse(response);
    if (!json) {
        snprintf(updater->error_message, sizeof(updater->error_message), "Failed to parse JSON response.");
        updater->status = UPDATE_STATUS_ERROR;
        goto cleanup;
    }

    const cJSON *tag_name_item = cJSON_GetObjectItem(json, "tag_name");
    if (!cJSON_IsString(tag_name_item)) {
        snprintf(updater->error_message, sizeof(updater->error_message), "No tag_name in release info.");
        updater->status = UPDATE_STATUS_ERROR;
        goto cleanup;
    }

    char remote_version_str[32];
    strncpy(remote_version_str, tag_name_item->valuestring, sizeof(remote_version_str) - 1);

    int remote_major, remote_minor, remote_patch;
    int current_major, current_minor, current_patch;

    if (parse_version(remote_version_str, &remote_major, &remote_minor, &remote_patch) != 3 ||
        parse_version(APP_VERSION, &current_major, &current_minor, &current_patch) != 3) {
        snprintf(updater->error_message, sizeof(updater->error_message), "Failed to parse version strings.");
        updater->status = UPDATE_STATUS_ERROR;
        goto cleanup;
    }

    bool new_version_is_available = (remote_major > current_major) ||
                                  (remote_major == current_major && remote_minor > current_minor) ||
                                  (remote_major == current_major && remote_minor == current_minor && remote_patch > current_patch);

    if (strcmp(remote_version_str, updater->last_ignored_version) != 0 && new_version_is_available) {
        strncpy(updater->latest_version, remote_version_str, sizeof(updater->latest_version) - 1);

        const cJSON *assets_array = cJSON_GetObjectItem(json, "assets");
        if (cJSON_IsArray(assets_array)) {
            const char* platform_str =
#if defined(__APPLE__) && defined(__aarch64__)
                "macos-arm64.dmg";
#elif defined(__APPLE__)
                "macos-x86_64.dmg";
#elif defined(_WIN32)
                "windows-x64.zip";
#else
                NULL;
#endif
            if (platform_str) {
                cJSON *asset;
                cJSON_ArrayForEach(asset, assets_array) {
                    const cJSON *name = cJSON_GetObjectItem(asset, "name");
                    const cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
                    if (cJSON_IsString(name) && strstr(name->valuestring, platform_str) && cJSON_IsString(url)) {
                        strncpy(updater->download_url, url->valuestring, sizeof(updater->download_url) - 1);
                        updater->status = UPDATE_STATUS_AVAILABLE;
                        break;
                    }
                }
            }
        }
    } else {
        updater->status = UPDATE_STATUS_IDLE;
    }

cleanup:
    if (json) {
        cJSON_Delete(json);
    }
    SDL_free(data);
}

void updater_check_for_updates(UpdaterState *updater, CurlManager* curl_manager) {
    if (updater->status == UPDATE_STATUS_CHECKING) {
        return;
    }
    updater->status = UPDATE_STATUS_CHECKING;

    UpdateCheckData *data = SDL_malloc(sizeof(UpdateCheckData));
    data->updater_state = updater;

    curl_manager_perform_get(curl_manager, GITHUB_API_URL, update_check_callback, data);
}

UpdaterState* updater_create(void) {
    UpdaterState* updater = SDL_calloc(1, sizeof(UpdaterState));
    if (!updater) {
        return NULL;
    }

    updater->status = UPDATE_STATUS_IDLE;
    updater->config_path = SDL_GetPrefPath(UPDATER_ORG, UPDATER_APP);
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

    updater->check_on_startup = true; // Default
    updater->last_ignored_version[0] = '\0';

    SDL_IOStream *file = SDL_IOFromFile(config_file_path, "r");
    if (!file) {
        return; // No config file, use defaults
    }

    long size = SDL_GetIOSize(file);
    char *buffer = (char *)SDL_malloc(size + 1);
    if (!buffer) {
        SDL_CloseIO(file);
        return;
    }
    SDL_ReadIO(file, buffer, size);
    buffer[size] = '\0';
    SDL_CloseIO(file);

    cJSON *json = cJSON_Parse(buffer);
    if (json) {
        const cJSON *check_on_startup_item = cJSON_GetObjectItem(json, "check_on_startup");
        if (cJSON_IsBool(check_on_startup_item)) {
            updater->check_on_startup = cJSON_IsTrue(check_on_startup_item);
        }

        const cJSON *last_ignored_item = cJSON_GetObjectItem(json, "last_ignored_version");
        if (cJSON_IsString(last_ignored_item)) {
            strncpy(updater->last_ignored_version, last_ignored_item->valuestring, sizeof(updater->last_ignored_version) - 1);
        }
        cJSON_Delete(json);
    }

    SDL_free(buffer);
}

void updater_save_config(UpdaterState* updater) {
    char config_file_path[1024];
    snprintf(config_file_path, sizeof(config_file_path), "%sconfig.json", updater->config_path);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddBoolToObject(root, "check_on_startup", updater->check_on_startup);
    cJSON_AddStringToObject(root, "last_ignored_version", updater->last_ignored_version);

    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_string) return;

    SDL_IOStream *file = SDL_IOFromFile(config_file_path, "w");
    if (file) {
        if (SDL_WriteIO(file, json_string, strlen(json_string)) < strlen(json_string)) {
            snprintf(updater->error_message, sizeof(updater->error_message), "Could not write to config.json: %s", SDL_GetError());
        }
        SDL_CloseIO(file);
    }

    free(json_string);
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
    DownloadCallbackData* data = (DownloadCallbackData*)userdata;
    UpdaterState* updater = data->updater;
    const char* base_path = data->base_path;

    if (success) {
        updater->status = UPDATE_STATUS_IDLE;
        printf("Update downloaded to: %s\n", downloaded_path);

#ifdef _WIN32
        char script_path[1024];
        char* pref_path = SDL_GetPrefPath(UPDATER_ORG, UPDATER_APP);
        snprintf(script_path, sizeof(script_path), "%s\\update.ps1", pref_path);
        SDL_free(pref_path);
        SDL_IOStream *file = SDL_IOFromFile(script_path, "w");
        if (file) {
            char script_content[2048];
            char* base_path_escaped = SDL_strdup(base_path);
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
        char* pref_path = SDL_GetPrefPath(UPDATER_ORG, UPDATER_APP);
        snprintf(script_path, sizeof(script_path), "%s/update.sh", pref_path);
        SDL_free(pref_path);
        SDL_IOStream *file = SDL_IOFromFile(script_path, "w");
        if (file) {
            char script_content[2048];
            char* app_path = SDL_strdup(base_path);
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
    SDL_free((void*)data->base_path);
    SDL_free(data);
}

static void on_update_download_progress(double progress, void* userdata) {
    UpdaterState* updater = (UpdaterState*)userdata;
    updater->download_progress = progress;
}

void updater_start_download(UpdaterState* updater, CurlManager* curl_manager, const char* base_path) {
    if (updater->status != UPDATE_STATUS_AVAILABLE) {
        return;
    }

    updater->status = UPDATE_STATUS_DOWNLOADING;
    updater->download_progress = 0.0;

    char temp_path[1024];
    char* pref_path = SDL_GetPrefPath(UPDATER_ORG, UPDATER_APP);
#ifdef _WIN32
    snprintf(temp_path, sizeof(temp_path), "%s\\update.zip", pref_path);
#else
    snprintf(temp_path, sizeof(temp_path), "%s/update.dmg", pref_path);
#endif
    SDL_free(pref_path);

    DownloadCallbackData* data = SDL_malloc(sizeof(DownloadCallbackData));
    data->updater = updater;
    data->base_path = SDL_strdup(base_path);

    curl_manager_download_file(
        curl_manager,
        updater->download_url,
        temp_path,
        on_update_download_complete,
        on_update_download_progress,
        data
    );
}
