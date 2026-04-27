/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_every.c - "every" command implementation
 *
 * Thin wrapper over tiku_shell_jobs_schedule_argv() that registers a
 * recurring job.  All parsing, validation, and diagnostics live in
 * the jobs subsystem; this file simply selects the EVERY job type.
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

#include "tiku_shell_cmd_every.h"
#include <kernel/shell/tiku_shell_jobs.h>

void
tiku_shell_cmd_every(uint8_t argc, const char *argv[])
{
    (void)tiku_shell_jobs_schedule_argv(TIKU_SHELL_JOB_EVERY, argc, argv);
}
