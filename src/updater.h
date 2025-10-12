#ifndef UPDATER_H
#define UPDATER_H

#include <stdbool.h>
#include "connections/curl_manager.h"

typedef enum {
    UPDATE_STATUS_IDLE,
    UPDATE_STATUS_CHECKING,
    UPDATE_STATUS_AVAILABLE,
    UPDATE_STATUS_DOWNLOADING,
    UPDATE_STATUS_ERROR
} UpdateStatus;

typedef struct {
    UpdateStatus status;
    char latest_version[32];
    char download_url[256];
    char error_message[256];
    double download_progress;
    bool check_on_startup;
    char last_ignored_version[32];
    char* config_path;
} UpdaterState;

UpdaterState* updater_create(const char* org, const char* app);
void updater_destroy(UpdaterState* updater);

void updater_check_for_updates(UpdaterState* updater);
void updater_start_download(UpdaterState* updater, CurlManager* curl_manager, const char* base_path);

void updater_load_config(UpdaterState* updater);
void updater_save_config(UpdaterState* updater);

#endif // UPDATER_H
