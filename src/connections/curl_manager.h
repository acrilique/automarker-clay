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

#ifndef CURL_MANAGER_H
#define CURL_MANAGER_H

#include <curl/curl.h>
#include <stdbool.h>

typedef struct CurlManager {
    CURLM *multi_handle;
    int still_running;
} CurlManager;

CurlManager* curl_manager_create();
void curl_manager_destroy(CurlManager *manager);
void curl_manager_add_handle(CurlManager *manager, CURL *easy_handle);
void curl_manager_update(CurlManager *manager);

void curl_manager_perform_get(CurlManager *manager, const char *url, void (*callback)(const char*, bool, void*), void *userdata);
void curl_manager_download_file(CurlManager *manager, const char *url, const char *output_path, void (*callback)(const char*, bool, void*), void (*progress_callback)(double, void*), void *userdata);

#endif // CURL_MANAGER_H
