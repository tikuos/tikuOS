/*
 * tiku.h -- the HOST's stand-in for the firmware umbrella header.
 *
 * The interpreter is compiled here exactly as it is for a board, with
 * every peripheral gated off; what a board's kernel provides -- a
 * console, a clock, an arena -- the harness beside this provides from
 * the host's own libc.  Nothing of the interpreter itself is changed,
 * which is the point: the program a person runs in the editor is run
 * by the same bytes a board would run it with.
 */
#ifndef TIKU_HOST_STUB_H_
#define TIKU_HOST_STUB_H_
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static inline void tiku_watchdog_kick(void) {}
#define TIKU_PRINTF(...) printf(__VA_ARGS__)
#endif
