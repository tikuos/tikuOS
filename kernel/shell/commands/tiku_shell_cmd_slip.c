/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_slip.c - "slip" command implementation
 *
 * Hands the console UART to the net process for SLIP/IP networking.  Net is
 * a compile-time choice (TIKU_KIT_NET_ENABLE=1 pulls in the stack); this
 * command is the runtime switch that activates it on the one shared UART,
 * so the same OS image is an interactive shell by default and a SLIP network
 * endpoint on demand.  The UART then carries binary SLIP framing, so the
 * shell can no longer read commands -- reset the board to return.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_slip.h"
#include <kernel/shell/tiku_shell.h>                 /* SHELL_PRINTF */
#include <kernel/process/tiku_process.h>             /* tiku_process_start */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>    /* tiku_kits_net_process */

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/* Once set, the shell loop yields all UART input to the net process. */
static uint8_t slip_mode;

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_shell_cmd_slip_active(void)
{
    return slip_mode;
}

void
tiku_shell_cmd_slip(uint8_t argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    if (slip_mode) {
        SHELL_PRINTF("Already in SLIP mode.\n");
        return;
    }

    SHELL_PRINTF("Handing the console UART to SLIP/IP.\n");
    SHELL_PRINTF("The shell is suspended; reset the board to return.\n");

    /*
     * Start the net process.  It initialises SLIP, registers the link
     * backend, and polls the UART for IP frames.  From here the UART carries
     * binary SLIP, so the shell loop yields all input to the net process via
     * tiku_shell_cmd_slip_active().
     */
    tiku_process_start(&tiku_kits_net_process, (tiku_event_data_t)0);
    slip_mode = 1;
}
