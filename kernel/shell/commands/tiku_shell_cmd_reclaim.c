/* Explicit reconstruction controls; never force a stop. SPDX-License-Identifier: Apache-2.0 */
#include "tiku_shell_cmd_reclaim.h"
#include "kernel/memory/tiku_reclaim_internal.h"
#include "kernel/shell/tiku_shell.h"
#include <string.h>

#if TIKU_MEM_RECLAIM_ENABLE
/* Shared kernel command scratch, not a process stack or a reclaimable arena. */
static char report[2048];
void tiku_shell_cmd_reclaim(uint8_t argc, const char *argv[])
{
    const char *entry;
    int result;
    if (argc < 2 || strcmp(argv[1], "reclaim")) goto usage;
    entry = argc == 2 ? "job" : argv[2];
    if (!strcmp(entry, "status")) entry = "job";
    if (argc == 4 && (!strcmp(entry, "mode") || !strcmp(entry, "retry") || !strcmp(entry, "cancel"))) {
        result = tiku_mem_reclaim_write(entry, argv[3], strlen(argv[3]));
        if (result != TIKU_MEM_OK) SHELL_PRINTF("mem reclaim: request refused (%d)\n", result);
        else SHELL_PRINTF("mem reclaim: accepted; use status to check completion\n");
        return;
    }
    if (argc > 3) goto usage;
    result = tiku_mem_reclaim_read(entry, report, sizeof report);
    if (result < 0) goto usage;
    if (result) SHELL_PRINTF("%s", report);
    else SHELL_PRINTF("none\n");
    return;
usage:
    SHELL_PRINTF("mem reclaim [status|last|pending|owners|stats|mode [off|reactive]|retry 1:GEN|cancel 1:GEN]\n");
}
#endif
