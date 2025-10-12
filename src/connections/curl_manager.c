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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration for the struct
typedef struct {
    char *data;
    struct curl_slist *headers;
} JsxRequestData;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

CurlManager* curl_manager_create() {
    CurlManager *manager = (CurlManager*)malloc(sizeof(CurlManager));
    if (manager) {
        manager->multi_handle = curl_multi_init();
        manager->still_running = 0;
    }
    return manager;
}

void curl_manager_destroy(CurlManager *manager) {
    if (manager) {
        curl_multi_cleanup(manager->multi_handle);
        free(manager);
    }
}

void curl_manager_add_handle(CurlManager *manager, CURL *easy_handle) {
    curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_multi_add_handle(manager->multi_handle, easy_handle);
}

void curl_manager_update(CurlManager *manager) {
    CURLMcode mc = curl_multi_perform(manager->multi_handle, &manager->still_running);

    if (mc != CURLM_OK) {
        fprintf(stderr, "curl_multi_perform() failed: %s\n", curl_multi_strerror(mc));
        return;
    }

    CURLMsg *m;
    do {
        int msgq = 0;
        m = curl_multi_info_read(manager->multi_handle, &msgq);
        if (m && (m->msg == CURLMSG_DONE)) {
            CURL *e = m->easy_handle;
            CURLcode res = m->data.result;

            if (res != CURLE_OK) {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                long response_code;
                curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &response_code);
                fprintf(stderr, "HTTP response code: %ld\n", response_code);
            }
            
            JsxRequestData *request_data;
            curl_easy_getinfo(e, CURLINFO_PRIVATE, &request_data);
            
            if (request_data) {
                free(request_data->data);
                curl_slist_free_all(request_data->headers);
                free(request_data);
            }

            curl_multi_remove_handle(manager->multi_handle, e);
            curl_easy_cleanup(e);
        }
    } while (m);
}

// --- GET Request Implementation ---

typedef struct {
    char *buffer;
    size_t size;
    void (*callback)(const char*, bool, void*);
    void *userdata;
} GetRequestData;

static size_t get_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    GetRequestData *mem = (GetRequestData *)userp;

    char *ptr = realloc(mem->buffer, mem->size + realsize + 1);
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
        GetRequestData *request_data = (GetRequestData*)malloc(sizeof(GetRequestData));
        request_data->buffer = malloc(1);
        request_data->size = 0;
        request_data->callback = callback;
        request_data->userdata = userdata;

        curl_easy_setopt(easy_handle, CURLOPT_URL, url);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, get_write_callback);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, (void *)request_data);
        curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, (void *)request_data);
        curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, "curl/7.81.0");

        curl_multi_add_handle(manager->multi_handle, easy_handle);
    }
}

// --- File Download Implementation ---

typedef struct {
    FILE *stream;
    void (*callback)(const char*, bool, void*);
    void (*progress_callback)(double, void*);
    void *userdata;
    char output_path[1024];
} DownloadRequestData;

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
        DownloadRequestData *request_data = (DownloadRequestData*)malloc(sizeof(DownloadRequestData));
        strncpy(request_data->output_path, output_path, sizeof(request_data->output_path) - 1);
        request_data->callback = callback;
        request_data->progress_callback = progress_callback;
        request_data->userdata = userdata;
        request_data->stream = fopen(output_path, "wb");

        if (!request_data->stream) {
            free(request_data);
            // TODO: Handle error
            return;
        }

        curl_easy_setopt(easy_handle, CURLOPT_URL, url);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, download_write_callback);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, request_data->stream);
        curl_easy_setopt(easy_handle, CURLOPT_XFERINFOFUNCTION, download_progress_callback);
        curl_easy_setopt(easy_handle, CURLOPT_XFERINFODATA, request_data);
        curl_easy_setopt(easy_handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, (void *)request_data);
        curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, "curl/7.81.0");
        curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 1L);

        curl_multi_add_handle(manager->multi_handle, easy_handle);
    }
}
