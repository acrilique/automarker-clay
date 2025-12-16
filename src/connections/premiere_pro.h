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

#include <SDL3/SDL.h>
#include "curl_manager.h"

/**
 * Status values for the CEP extension installation process.
 * 
 * Thread-safety note: Status transitions are performed atomically via SDL atomic
 * operations. Terminal states (CEP_INSTALL_SUCCESS, CEP_INSTALL_ERROR) are set
 * only after all associated data (e.g., error_message) has been written.
 */
typedef enum {
    CEP_INSTALL_IDLE,          /**< No installation in progress */
    CEP_INSTALL_IN_PROGRESS,   /**< Installation is currently running */
    CEP_INSTALL_SUCCESS,       /**< Installation completed successfully */
    CEP_INSTALL_ERROR          /**< Installation failed; see error_message */
} CepInstallStatus;

/**
 * This structure is shared between a worker thread (install_cep_thread) and 
 * the UI thread. To avoid data races, the following rules MUST be observed:
 * 
 * 1. The `status` field MUST always be accessed via SDL atomic APIs:
 * 
 * 2. The `error_message` field is written by the worker thread and may only
 *    be read by the UI thread AFTER observing a terminal status value
 *    (CEP_INSTALL_SUCCESS or CEP_INSTALL_ERROR) via SDL_GetAtomicInt.
 * 
 * 3. The worker thread MUST write error_message BEFORE setting a terminal
 *    status. SDL atomic operations provide release/acquire semantics that
 *    ensure the error_message write is visible once the status is observed.
 */
typedef struct {
    SDL_AtomicInt status;       /**< Current installation status (use SDL atomic APIs) */
    char error_message[256];    /**< Error details; valid only after CEP_INSTALL_ERROR */
} CepInstallState;

void install_cep_extension(const char *base_path, CepInstallState *state);
int premiere_pro_add_markers(CurlManager *curl_manager, const double *beats, int num_beats);
int premiere_pro_clear_all_markers(CurlManager *curl_manager);

/**
 * Check if the CEP panel is running and responding.
 * Sends a GET request to http://127.0.0.1:3000 and checks if the response
 * contains "Premiere is alive".
 *
 * @param curl_manager The curl manager to use for the request.
 * @param callback Function to call with the result (true if healthy, false otherwise).
 * @param userdata User data to pass to the callback.
 */
void premiere_pro_check_health(CurlManager *curl_manager, void (*callback)(bool healthy, void *userdata), void *userdata);

#endif // PREMIERE_PRO_H
