/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_basic.c - "basic" shell command stub.
 *
 * Thin dispatch wrapper that the shell command table calls.  The
 * actual interpreter engine lives at kernel/shell/basic/ and is
 * exposed via tiku_basic.h:
 *
 *   `basic`     -> tiku_basic_repl()      (interactive REPL)
 *   `basic run` -> tiku_basic_autorun()   (load + run saved prog)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_basic.h"
#include <kernel/shell/basic/tiku_basic.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_basic(uint8_t argc, const char *argv[])
{
    if (argc >= 2u && argv[1] != NULL && strcmp(argv[1], "run") == 0) {
        tiku_basic_autorun();
        return;
    }
    tiku_basic_repl();
}
