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

#ifndef PREMIERE_PRO_H
#define PREMIERE_PRO_H

#include "curl_manager.h"

typedef enum {
    CEP_INSTALL_IDLE,
    CEP_INSTALL_IN_PROGRESS,
    CEP_INSTALL_SUCCESS,
    CEP_INSTALL_ERROR
} CepInstallStatus;

typedef struct {
    CepInstallStatus status;
    char error_message[256];
} CepInstallState;

void install_cep_extension(const char *base_path, CepInstallState *state);
int premiere_pro_add_markers(CurlManager *curl_manager, const double *beats, int num_beats);
int premiere_pro_clear_all_markers(CurlManager *curl_manager);

#endif // PREMIERE_PRO_H
