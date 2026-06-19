/*
 * Backward compatibility — the CLI has moved to kernel/shell/.
 * This header forwards to the new location.
 */

#ifndef TIKU_CLI_H_
#define TIKU_CLI_H_

#include <kernel/shell/tiku_shell.h>

/* Legacy type / function aliases */
typedef tiku_shell_cmd_t     tiku_cli_cmd_t;
typedef tiku_shell_handler_t tiku_cli_handler_t;

#define tiku_cli_get_commands  tiku_shell_get_commands
#define tiku_cli_process       tiku_shell_process

#define CLI_PRINTF             SHELL_PRINTF
#define TIKU_CLI_LINE_SIZE     TIKU_SHELL_LINE_SIZE
#define TIKU_CLI_MAX_ARGS      TIKU_SHELL_MAX_ARGS

#endif /* TIKU_CLI_H_ */
