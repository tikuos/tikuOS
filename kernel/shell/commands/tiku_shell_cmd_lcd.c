/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_lcd.c - "lcd" shell command
 *
 * Sub-commands:
 *   lcd info
 *   lcd clear
 *   lcd char    <pos> <ch>
 *   lcd puts    <text...>            (left-aligned, blanks rest)
 *   lcd putsr   <text...>            (right-aligned, blanks left)
 *   lcd putsa   <pos> <text...>      (partial overwrite at pos)
 *   lcd putu    <decimal>            (right-aligned unsigned)
 *   lcd puti    <decimal>            (right-aligned signed)
 *   lcd puth    <hex> [digits]       (right-aligned uppercase hex)
 *   lcd icon    <id> <on|off|toggle>
 *   lcd icons   clear
 *
 * The text arguments are joined with single spaces, mirroring the
 * way `echo` reconstructs sentences from argv. Numeric arguments
 * accept "0x"-prefixed hex.
 *
 * Designed to pair with TikuBench/tikubench/lcd_test.py, which
 * paints a frame and prompts the user for visual confirmation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_lcd.h"
#include <kernel/shell/tiku_shell.h>
#include <interfaces/lcd/tiku_lcd.h>
#include <stddef.h>

#define LCD_TEXT_BUF_LEN    24

/*---------------------------------------------------------------------------*/
/* Helpers                                                                    */
/*---------------------------------------------------------------------------*/

/* Concatenate argv[start..argc-1] into out, joined by single spaces.
 * Always nul-terminates. Truncates if the joined string would exceed
 * (out_len - 1). */
static void
join_args(char *out, size_t out_len,
          uint8_t argc, const char *argv[], uint8_t start)
{
    size_t  cursor = 0;
    uint8_t i;
    size_t  k;

    if (out_len == 0) {
        return;
    }

    for (i = start; i < argc; i++) {
        if (i > start && cursor < out_len - 1) {
            out[cursor++] = ' ';
        }
        for (k = 0; argv[i][k] != '\0' && cursor < out_len - 1; k++) {
            out[cursor++] = argv[i][k];
        }
    }
    out[cursor] = '\0';
}

/* Parse a decimal string. Returns 1 on success and writes the value
 * to *out, 0 on parse failure (out untouched). Accepts an optional
 * leading '-' for the int variant. */
static uint8_t
parse_uint(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    uint8_t  any = 0;
    size_t   k;

    if (s == NULL) {
        return 0;
    }

    /* "0x.." → hex */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (k = 0; s[k] != '\0'; k++) {
            uint8_t c = (uint8_t)s[k];
            uint8_t nib;
            if (c >= '0' && c <= '9') {
                nib = (uint8_t)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nib = (uint8_t)(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                nib = (uint8_t)(c - 'A' + 10);
            } else {
                return 0;
            }
            v = (v << 4) | nib;
            any = 1;
        }
    } else {
        for (k = 0; s[k] != '\0'; k++) {
            if (s[k] < '0' || s[k] > '9') {
                return 0;
            }
            v = v * 10U + (uint32_t)(s[k] - '0');
            any = 1;
        }
    }

    if (!any) {
        return 0;
    }
    *out = v;
    return 1;
}

static uint8_t
parse_int(const char *s, int32_t *out)
{
    uint32_t mag;
    uint8_t  neg = 0;

    if (s == NULL) {
        return 0;
    }
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (!parse_uint(s, &mag)) {
        return 0;
    }
    *out = neg ? -(int32_t)mag : (int32_t)mag;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* Sub-commands                                                               */
/*---------------------------------------------------------------------------*/

static void
do_info(void)
{
    SHELL_PRINTF("present:    %u\n", (unsigned)TIKU_LCD_PRESENT);
    SHELL_PRINTF("num_chars:  %u\n", (unsigned)tiku_lcd_num_chars());
#if defined(TIKU_BOARD_LCD_HAS_ICONS) && TIKU_BOARD_LCD_HAS_ICONS
    SHELL_PRINTF("num_icons:  %u\n", (unsigned)tiku_lcd_icon_count());
#else
    SHELL_PRINTF("num_icons:  0\n");
#endif
}

static void
do_text(uint8_t argc, const char *argv[],
        uint8_t start, void (*fn)(const char *))
{
    char buf[LCD_TEXT_BUF_LEN];

    if (argc <= start) {
        fn("");
    } else {
        join_args(buf, sizeof(buf), argc, argv, start);
        fn(buf);
    }
}

static void
do_putsa(uint8_t argc, const char *argv[])
{
    char     buf[LCD_TEXT_BUF_LEN];
    uint32_t pos;

    if (argc < 4 || !parse_uint(argv[2], &pos)) {
        SHELL_PRINTF("Usage: lcd putsa <pos> <text...>\n");
        return;
    }
    join_args(buf, sizeof(buf), argc, argv, 3);
    tiku_lcd_puts_at((uint8_t)pos, buf);
}

static void
do_char(uint8_t argc, const char *argv[])
{
    uint32_t pos;

    if (argc < 4 || !parse_uint(argv[2], &pos) || argv[3][0] == '\0') {
        SHELL_PRINTF("Usage: lcd char <pos> <ch>\n");
        return;
    }
    tiku_lcd_putchar((uint8_t)pos, argv[3][0]);
}

static void
do_putu(uint8_t argc, const char *argv[])
{
    uint32_t v;
    if (argc < 3 || !parse_uint(argv[2], &v)) {
        SHELL_PRINTF("Usage: lcd putu <decimal>\n");
        return;
    }
    tiku_lcd_put_uint(v);
}

static void
do_puti(uint8_t argc, const char *argv[])
{
    int32_t v;
    if (argc < 3 || !parse_int(argv[2], &v)) {
        SHELL_PRINTF("Usage: lcd puti <signed-decimal>\n");
        return;
    }
    tiku_lcd_put_int(v);
}

static void
do_putf(uint8_t argc, const char *argv[])
{
    int32_t  v;
    uint32_t dec;

    if (argc < 4 || !parse_int(argv[2], &v) || !parse_uint(argv[3], &dec)) {
        SHELL_PRINTF("Usage: lcd putf <signed-decimal> <decimals>\n");
        return;
    }
    tiku_lcd_put_fixed(v, (uint8_t)dec);
}

static void
do_puth(uint8_t argc, const char *argv[])
{
    uint32_t v;
    uint32_t digits = (uint32_t)tiku_lcd_num_chars();

    if (argc < 3 || !parse_uint(argv[2], &v)) {
        SHELL_PRINTF("Usage: lcd puth <hex> [digits]\n");
        return;
    }
    if (argc >= 4) {
        if (!parse_uint(argv[3], &digits) || digits == 0) {
            SHELL_PRINTF("Usage: lcd puth <hex> [digits]\n");
            return;
        }
    }
    tiku_lcd_put_hex(v, (uint8_t)digits);
}

static void
do_icon(uint8_t argc, const char *argv[])
{
#if defined(TIKU_BOARD_LCD_HAS_ICONS) && TIKU_BOARD_LCD_HAS_ICONS
    uint32_t id;
    const char *act;

    if (argc < 4 || !parse_uint(argv[2], &id)) {
        SHELL_PRINTF("Usage: lcd icon <id> <on|off|toggle>\n");
        return;
    }
    act = argv[3];
    if (act[0] == 'o' && act[1] == 'n' && act[2] == '\0') {
        tiku_lcd_icon_on((uint8_t)id);
    } else if (act[0] == 'o' && act[1] == 'f' && act[2] == 'f') {
        tiku_lcd_icon_off((uint8_t)id);
    } else if (act[0] == 't') {
        tiku_lcd_icon_toggle((uint8_t)id);
    } else {
        SHELL_PRINTF("Usage: lcd icon <id> <on|off|toggle>\n");
    }
#else
    (void)argc;
    (void)argv;
    SHELL_PRINTF("Board has no named LCD icons.\n");
#endif
}

static void
do_icons(uint8_t argc, const char *argv[])
{
#if defined(TIKU_BOARD_LCD_HAS_ICONS) && TIKU_BOARD_LCD_HAS_ICONS
    if (argc >= 3 && argv[2][0] == 'c') {
        tiku_lcd_icons_clear();
        return;
    }
    SHELL_PRINTF("Usage: lcd icons clear\n");
#else
    (void)argc;
    (void)argv;
    SHELL_PRINTF("Board has no named LCD icons.\n");
#endif
}

static void
print_help(void)
{
    SHELL_PRINTF(
        "Usage: lcd <subcommand> [args]\n"
        "  info                       show panel size + icon count\n"
        "  clear                      blank everything\n"
        "  char  <pos> <ch>           one character at pos\n"
        "  puts  <text...>            left-aligned text\n"
        "  putsr <text...>            right-aligned text\n"
        "  putsa <pos> <text...>      partial write at pos\n"
        "  putu  <decimal>            right-aligned unsigned\n"
        "  puti  <signed-decimal>     right-aligned signed\n"
        "  puth  <hex> [digits]       right-aligned hex\n"
        "  putf  <int> <decimals>     fixed-point with dot icon\n"
        "  icon  <id> <on|off|toggle> set/clear/toggle icon\n"
        "  icons clear                clear all icons\n");
}

/*---------------------------------------------------------------------------*/
/* Entry                                                                      */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_lcd(uint8_t argc, const char *argv[])
{
    static uint8_t lcd_inited = 0;
    const char    *sub;

    if (argc < 2) {
        print_help();
        return;
    }

    if (!TIKU_LCD_PRESENT) {
        SHELL_PRINTF("No LCD on this board.\n");
        return;
    }

    /* Lazy bring-up: if no autostart process (e.g. the LCD demo)
     * has already initialised the LCD_C peripheral, the panel is
     * unconfigured and writes to LCDMEM produce nothing visible.
     * Init on first use so the harness works in shell-only builds. */
    if (!lcd_inited) {
        tiku_lcd_init();
        lcd_inited = 1;
    }

    sub = argv[1];

    if (sub[0] == 'i' && sub[1] == 'n') {
        do_info();
    } else if (sub[0] == 'c' && sub[1] == 'l') {
        tiku_lcd_clear();
    } else if (sub[0] == 'c' && sub[1] == 'h') {
        do_char(argc, argv);
    } else if (sub[0] == 'p' && sub[1] == 'u' && sub[2] == 't') {
        if (sub[3] == 's' && sub[4] == 'a') {
            do_putsa(argc, argv);
        } else if (sub[3] == 's' && sub[4] == 'r') {
            do_text(argc, argv, 2, tiku_lcd_puts_right);
        } else if (sub[3] == 's' && sub[4] == '\0') {
            do_text(argc, argv, 2, tiku_lcd_puts);
        } else if (sub[3] == 'u') {
            do_putu(argc, argv);
        } else if (sub[3] == 'i') {
            do_puti(argc, argv);
        } else if (sub[3] == 'h') {
            do_puth(argc, argv);
        } else if (sub[3] == 'f') {
            do_putf(argc, argv);
        } else {
            print_help();
        }
    } else if (sub[0] == 'i' && sub[1] == 'c' && sub[2] == 'o' &&
               sub[3] == 'n' && sub[4] == 's') {
        do_icons(argc, argv);
    } else if (sub[0] == 'i' && sub[1] == 'c' && sub[2] == 'o' &&
               sub[3] == 'n') {
        do_icon(argc, argv);
    } else {
        print_help();
    }
}
