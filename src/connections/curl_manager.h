#ifndef CURL_MANAGER_H
#define CURL_MANAGER_H

#include <curl/curl.h>

typedef struct {
    CURLM *multi_handle;
    int still_running;
} CurlManager;

CurlManager* curl_manager_create();
void curl_manager_destroy(CurlManager *manager);
void curl_manager_add_handle(CurlManager *manager, CURL *easy_handle);
void curl_manager_update(CurlManager *manager);

#endif // CURL_MANAGER_H
