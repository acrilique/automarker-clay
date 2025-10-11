#include "curl_manager.h"
#include <stdio.h>
#include <stdlib.h>

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
