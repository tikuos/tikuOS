#ifndef TIKU_HOST_SHELL_STUB_H_
#define TIKU_HOST_SHELL_STUB_H_
/*
 * The console the interpreter speaks and the pump it yields to, as the
 * HOST provides them.  Colour codes are empty on purpose: this output
 * lands in an editor's pane, not on a terminal that renders escapes.
 */
#include <stdio.h>
#include <tiku.h>
/* Routed through the harness, which drops the REPL's prompt and banner:
 * an editor's output pane wants what the PROGRAM said, and "ok> " is
 * the conversation's furniture rather than its words.  The prompt is
 * also how the harness HEARS that a program finished. */
void tiku_basic_host_printf(const char *fmt, ...);
#define SHELL_PRINTF(...) tiku_basic_host_printf(__VA_ARGS__)
#define SH_RED    ""
#define SH_GREEN  ""
#define SH_YELLOW ""
#define SH_CYAN   ""
#define SH_BOLD   ""
#define SH_DIM    ""
#define SH_RST    ""
#define SHELL_POLL_TICKS 1
int  tiku_shell_io_rx_ready(void);
int  tiku_shell_io_getc(void);
int  tiku_shell_io_has_echo(void);
int  tiku_shell_net_getc(void);
void tiku_shell_net_pump(void);
void tiku_shell_pump_net(void);
void tiku_shell_pump(void);
int  tiku_shell_process(const char *line);

/* The process the arena is attached to: on a board this is the shell's
 * own; the host harness has one process and it is this one. */
struct tiku_process;
#define TIKU_THIS() ((struct tiku_process *)0)
static inline void tiku_process_attach_mem_arena(struct tiku_process *p,
                                                 void *arena) {
    (void)p; (void)arena;
}
static inline void tiku_process_queue_dispatchable_except(
    struct tiku_process *p) { (void)p; }
static inline void tiku_process_run_except(struct tiku_process *p) {
    (void)p;
}
#endif
