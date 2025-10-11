#ifndef PREMIERE_PRO_H
#define PREMIERE_PRO_H

#include "curl_manager.h"

void install_cep_extension(const char *base_path);
int premiere_pro_add_markers(CurlManager *curl_manager, const double *beats, int num_beats);
int premiere_pro_clear_all_markers(CurlManager *curl_manager);

#endif // PREMIERE_PRO_H
