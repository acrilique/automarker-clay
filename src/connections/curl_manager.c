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

#include "curl_manager.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Data Structures ---
typedef struct {
    char *buffer;
    size_t size;
    void (*callback)(const char*, bool, void*);
    void *userdata;
} GetRequestData;

typedef struct {
    FILE *stream;
    void (*callback)(const char*, bool, void*);
    void (*progress_callback)(double, void*);
    void *userdata;
    char output_path[1024];
} DownloadRequestData;

typedef struct {
    char *data;
    struct curl_slist *headers;
} JsxRequestData;

typedef struct {
    RequestType type;
    void* data;
} RequestData;

CurlManager* curl_manager_create() {
    CurlManager *manager = (CurlManager*)SDL_malloc(sizeof(CurlManager));
    if (manager) {
        manager->multi_handle = curl_multi_init();
        manager->still_running = 0;
    }
    return manager;
}

void curl_manager_destroy(CurlManager *manager) {
    if (manager) {
        curl_multi_cleanup(manager->multi_handle);
        SDL_free(manager);
    }
}

void curl_manager_add_handle(CurlManager *manager, CURL *easy_handle, RequestType type, void* data) {
    RequestData* request_data = SDL_malloc(sizeof(RequestData));
    request_data->type = type;
    request_data->data = data;
    curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, request_data);
    curl_multi_add_handle(manager->multi_handle, easy_handle);
}

void curl_manager_update(CurlManager *manager) {
    curl_multi_perform(manager->multi_handle, &manager->still_running);

    CURLMsg *msg;
    int msgs_left;
    while ((msg = curl_multi_info_read(manager->multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy_handle = msg->easy_handle;
            CURLcode result = msg->data.result;
            
            RequestData* request_data;
            curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &request_data);

            bool success = (result == CURLE_OK);

            switch (request_data->type) {
                case REQUEST_TYPE_GET: {
                    GetRequestData* get_data = (GetRequestData*)request_data->data;
                    if (get_data->callback) {
                        get_data->callback(get_data->buffer, success, get_data->userdata);
                    }
                    SDL_free(get_data->buffer);
                    SDL_free(get_data);
                    break;
                }
                case REQUEST_TYPE_DOWNLOAD: {
                    DownloadRequestData* dl_data = (DownloadRequestData*)request_data->data;
                    if (dl_data->stream) {
                        fclose(dl_data->stream);
                    }
                    if (dl_data->callback) {
                        dl_data->callback(dl_data->output_path, success, dl_data->userdata);
                    }
                    SDL_free(dl_data);
                    break;
                }
                case REQUEST_TYPE_JSX: {
                    JsxRequestData* jsx_data = (JsxRequestData*)request_data->data;
                    // Assuming no callback for JSX, just cleanup
                    if (jsx_data) {
                        SDL_free(jsx_data->data);
                        curl_slist_free_all(jsx_data->headers);
                        SDL_free(jsx_data);
                    }
                    break;
                }
            }
            
            SDL_free(request_data);
            curl_multi_remove_handle(manager->multi_handle, easy_handle);
            curl_easy_cleanup(easy_handle);
        }
    }
}

// --- GET Request Implementation ---
static size_t get_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    GetRequestData *mem = (GetRequestData *)userp;

    char *ptr = SDL_realloc(mem->buffer, mem->size + realsize + 1);
    if(ptr == NULL) {
        /* out of memory! */ 
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->buffer = ptr;
    memcpy(&(mem->buffer[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->buffer[mem->size] = 0;

    return realsize;
}

void curl_manager_perform_get(CurlManager *manager, const char *url, void (*callback)(const char*, bool, void*), void *userdata) {
    CURL *easy_handle = curl_easy_init();
    if (easy_handle) {
        GetRequestData *get_data = (GetRequestData*)SDL_malloc(sizeof(GetRequestData));
        get_data->buffer = SDL_malloc(1);
        get_data->size = 0;
        get_data->callback = callback;
        get_data->userdata = userdata;

        curl_easy_setopt(easy_handle, CURLOPT_URL, url);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, get_write_callback);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, (void *)get_data);
        curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, "curl/7.81.0");

        curl_manager_add_handle(manager, easy_handle, REQUEST_TYPE_GET, get_data);
    }
}

// --- File Download Implementation ---
static size_t download_write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}

static int download_progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    DownloadRequestData *data = (DownloadRequestData *)clientp;
    if (dltotal > 0 && data->progress_callback) {
        data->progress_callback((double)dlnow / (double)dltotal, data->userdata);
    }
    return 0;
}

void curl_manager_download_file(CurlManager *manager, const char *url, const char *output_path, void (*callback)(const char*, bool, void*), void (*progress_callback)(double, void*), void *userdata) {
    CURL *easy_handle = curl_easy_init();
    if (easy_handle) {
        DownloadRequestData *dl_data = (DownloadRequestData*)SDL_malloc(sizeof(DownloadRequestData));
        strncpy(dl_data->output_path, output_path, sizeof(dl_data->output_path) - 1);
        dl_data->callback = callback;
        dl_data->progress_callback = progress_callback;
        dl_data->userdata = userdata;
        dl_data->stream = fopen(output_path, "wb");

        if (!dl_data->stream) {
            SDL_free(dl_data);
            // TODO: Handle error
            return;
        }

        curl_easy_setopt(easy_handle, CURLOPT_URL, url);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, download_write_callback);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, dl_data->stream);
        curl_easy_setopt(easy_handle, CURLOPT_XFERINFOFUNCTION, download_progress_callback);
        curl_easy_setopt(easy_handle, CURLOPT_XFERINFODATA, dl_data);
        curl_easy_setopt(easy_handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, "curl/7.81.0");
        curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 1L);

        curl_manager_add_handle(manager, easy_handle, REQUEST_TYPE_DOWNLOAD, dl_data);
    }
}
