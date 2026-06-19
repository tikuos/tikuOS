/*
 * Backward compatibility — the CLI has moved to kernel/shell/.
 * This header forwards to the new location.
 */

#ifndef TIKU_CLI_IO_H_
#define TIKU_CLI_IO_H_

#include <kernel/shell/tiku_shell_io.h>

typedef tiku_shell_io_t tiku_cli_io_t;

#define tiku_cli_io_uart       tiku_shell_io_uart
#define tiku_cli_io_set_backend tiku_shell_io_set_backend
#define tiku_cli_io_get_backend tiku_shell_io_get_backend
#define CLI_PRINTF              SHELL_PRINTF

#define TIKU_CLI_IO_CRLF       TIKU_SHELL_IO_CRLF
#define TIKU_CLI_IO_ECHO       TIKU_SHELL_IO_ECHO

#endif /* TIKU_CLI_IO_H_ */
