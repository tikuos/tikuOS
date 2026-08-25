/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_syntax.c - telling one line of TikuOS BASIC apart.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "tiku_syntax.h"

/*
 * Every spelling the interpreter recognises, sorted so a word can be
 * found by halving rather than by walking.
 *
 * The list is not invented.  It is the union of three things the
 * interpreter itself holds: the crunch table BASIC_TOK_LIST (106 words,
 * the ones that fold to a byte when a numbered line is stored), the
 * spellings the dispatchers match by hand and never crunch (68 more --
 * a word is no less reserved for being spelled out), and the six the
 * bundled extension kit registers at boot (GCD, ISQRT, BITCNT, HEXPR,
 * REV$, ROMAN$), which are refused as variable names for as long as they
 * are registered.
 *
 * THE ONE THING THIS OVER-PROMISES, said plainly rather than discovered:
 * most of these words are compiled out of a small build (PEEK and POKE,
 * the GPIO and BLE and HTTP families, the whole `$` sub-language), and
 * on a build without it a compiled-out word is an ordinary variable
 * name.  An editor on a desktop cannot know which tier the board on the
 * other end of the cable was built at, so it colours the language rather
 * than the build.  Narrowing that would mean asking the device what it
 * has -- which the channel could do, and which nothing has needed yet.
 */
static const char *const basic_word[] = {
    "ABS", "ADC", "AND", "APPEND", "ASC", "ATAN", "AUTO", "BASE64$",
    "BETWEEN$", "BIN$", "BITCNT", "BLEADV", "BLEAVAIL", "BLEBEACON",
    "BLEGET$", "BLEOBSERVE", "BLEOFF", "BLESCAN$", "BLESEEN", "BLESEEN$",
    "BLESEND", "BLEUP", "BROWSE", "BYE", "CALL", "CASE", "CHANGE", "CHR$",
    "CLS", "CONST", "CONTINUE", "COS", "COUNT", "DATA", "DATE$", "DEF",
    "DELAY", "DIGREAD", "DIGWRITE", "DIM", "DIR", "ELSE", "ELSEIF", "END",
    "ENDIF", "ENDSUB", "ERL", "ERR", "ERROR", "EVERY", "EXIT", "EXP",
    "FALSE", "FDIV", "FETCH", "FMUL", "FN", "FOR", "FPOW", "FREAD$",
    "FSTR$", "FWRITE", "GCD", "GOSUB", "GOTO", "HELP", "HEX$", "HEXPR",
    "HMAC$", "HTTPGET$", "HTTPHEADER", "HTTPPOST$", "HTTPSTATUS", "I2CREAD",
    "I2CWRITE", "IF", "IMPORT", "INKEY$", "INPUT", "INSTR", "INT",
    "IPADDR$", "ISQRT", "JSON$", "LCASE$", "LED", "LEFT$", "LEN", "LET",
    "LINE$", "LIST", "LOAD", "LOCAL", "LOG", "LOWER$", "LTRIM$", "MAX",
    "MID$", "MILLIS", "MIN", "MOD", "MODACT", "MODLOAD", "MQTTPUB",
    "MQTTWAIT$", "NETUP", "NEW", "NEXT", "NOT", "NOW", "OFF", "ON", "OR",
    "PEEK", "PERSIST", "PI", "PIN", "POKE", "POW", "PRINT", "QUIT", "READ",
    "REBOOT", "REM", "RENUM", "REPEAT", "REPLACE$", "RESTORE", "RESULT",
    "RESUME", "RETURN", "REV$", "RIGHT$", "RND", "ROMAN$", "RTRIM$", "RUN",
    "SAVE", "SECS", "SELECT", "SETTIME", "SGN", "SHA256$", "SHL", "SHR",
    "SIN", "SLEEP", "SPACE$", "SPC", "SQR", "STEP", "STOP", "STR$",
    "STRING$", "STRIP$", "SUB", "SWAP", "TAB", "TAN", "THEN", "TIME$",
    "TIMER", "TO", "TRACE", "TRIM$", "TRUE", "UCASE$", "UDPSEND", "UNTIL",
    "UPPER$", "USING", "VAL", "VFSREAD", "VFSREAD$", "VFSWRITE",
    "VFSWRITE$", "WEND", "WHILE", "WORD$", "XOR",
};

#define BASIC_WORD_N ((int)(sizeof basic_word / sizeof basic_word[0]))

/** @brief The longest spelling above, plus its terminator. */
#define BASIC_WORD_MAX 16

static int
is_alpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int
is_digit(int c)
{
    return c >= '0' && c <= '9';
}

/** @brief What may continue a word once it has started (the lexer's rule). */
static int
is_word_cont(int c)
{
    return is_alpha(c) || is_digit(c) || c == '_';
}

static int
is_hex(int c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int
up(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

int
tiku_syntax_basic_word(const char *name)
{
    char folded[BASIC_WORD_MAX];
    int lo = 0, hi = BASIC_WORD_N - 1;
    size_t i, n;

    if (name == NULL) {
        return 0;
    }
    n = strlen(name);
    if (n == 0u || n >= sizeof folded) {
        /* Longer than any spelling: it cannot be one of them, and
         * folding it would be the truncation that makes VFSWRITELONG
         * read as VFSWRITE. */
        return 0;
    }
    for (i = 0u; i < n; i++) {
        folded[i] = (char)up((unsigned char)name[i]);
    }
    folded[n] = '\0';
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(folded, basic_word[mid]);

        if (cmp == 0) {
            return 1;
        }
        if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return 0;
}

tiku_syntax_lang_t
tiku_syntax_of_path(const char *path)
{
    size_t n;

    if (path == NULL) {
        return TIKU_SYNTAX_NONE;
    }
    n = strlen(path);
    if (n >= 4u) {
        const char *tail = path + n - 4;

        if (tail[0] == '.' && up((unsigned char)tail[1]) == 'B' &&
            up((unsigned char)tail[2]) == 'A' &&
            up((unsigned char)tail[3]) == 'S') {
            return TIKU_SYNTAX_BASIC;
        }
    }
    return TIKU_SYNTAX_NONE;
}

/*---------------------------------------------------------------------------*/
/* Telling a line apart                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Where the run table is being built, and how much room is left. */
typedef struct {
    tiku_span_t *out;
    int          max;
    int          n;
} spans_t;

/**
 * @brief Add @p len bytes meaning @p ink, merging with the run before it
 *        when they mean the same thing.
 *
 * Merging is what keeps an ordinary line to one run: without it every
 * space and every letter would be its own entry and the table would
 * overflow on a line nobody would call complicated.
 *
 * @return nonzero while there is still room to say more.
 */
static int
push(spans_t *s, tiku_ink_t ink, int len)
{
    if (len <= 0) {
        return 1;
    }
    if (s->n > 0 && s->out[s->n - 1].ink == ink) {
        s->out[s->n - 1].len += len;
        return 1;
    }
    if (s->n >= s->max) {
        return 0;               /* full: the rest of the line is plain */
    }
    s->out[s->n].ink = ink;
    s->out[s->n].len = len;
    s->n++;
    return 1;
}

/**
 * @brief How many bytes of a number literal start at @p p, or 0.
 *
 * Every form parse_unum() accepts and no others: 0x/0X and 0b/0B, &H/&h
 * and &B/&b, and decimal with an optional fractional part.  There is no
 * exponent form in this language, no leading-dot form, and no sign -- a
 * minus is an operator, and colouring it as part of the number would
 * paint `A-1` as if it held one.
 */
static int
number_at(const char *p)
{
    int i = 0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && is_hex(p[2])) {
        i = 2;
        while (is_hex(p[i])) {
            i++;
        }
        return i;
    }
    if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B') &&
        (p[2] == '0' || p[2] == '1')) {
        i = 2;
        while (p[i] == '0' || p[i] == '1') {
            i++;
        }
        return i;
    }
    if (p[0] == '&' && (p[1] == 'H' || p[1] == 'h') && is_hex(p[2])) {
        i = 2;
        while (is_hex(p[i])) {
            i++;
        }
        return i;
    }
    if (p[0] == '&' && (p[1] == 'B' || p[1] == 'b') &&
        (p[2] == '0' || p[2] == '1')) {
        i = 2;
        while (p[i] == '0' || p[i] == '1') {
            i++;
        }
        return i;
    }
    if (!is_digit((unsigned char)p[0])) {
        return 0;
    }
    while (is_digit((unsigned char)p[i])) {
        i++;
    }
    if (p[i] == '.') {
        int j = i + 1;

        while (is_digit((unsigned char)p[j])) {
            j++;
        }
        i = j;                  /* `1.` is a number too: the scale is Q.3 */
    }
    return i;
}

/**
 * @brief How many bytes the quoted string starting at @p p occupies.
 *
 * A backslash takes the next byte with it, so `"a\"b"` is one string and
 * not two.  An unterminated string runs to the end of the line, because
 * that is exactly what the interpreter does with it -- it does not
 * complain, so neither may the colour.
 */
static int
string_at(const char *p)
{
    int i = 1;                  /* past the opening quote */

    while (p[i] != '\0') {
        if (p[i] == '\\' && p[i + 1] != '\0') {
            i += 2;
            continue;
        }
        if (p[i] == '"') {
            return i + 1;
        }
        i++;
    }
    return i;
}

/** @brief The word at @p p: letters, then letters/digits/_, then a `$`. */
static int
word_at(const char *p)
{
    int i = 0;

    if (!is_alpha((unsigned char)p[0])) {
        return 0;
    }
    while (is_word_cont((unsigned char)p[i])) {
        i++;
    }
    if (p[i] == '$') {
        /* The sigil must touch the name -- `A $` is not `A$` -- which is
         * the same rule that makes STR$ one word and not two. */
        i++;
    }
    return i;
}

/**
 * @brief Whether the line opens with a label definition, and how long it
 *        is including the colon.
 *
 * A name of two or more characters followed by `:` at the head of a line
 * is a jump target.  One character is not: `A:` is the variable A and
 * then a statement separator, which is what the interpreter reads.
 */
static int
label_at(const char *p)
{
    int i = 0;

    if (!is_alpha((unsigned char)p[0])) {
        return 0;
    }
    while (is_word_cont((unsigned char)p[i])) {
        i++;
    }
    return (i >= 2 && p[i] == ':') ? i + 1 : 0;
}

/**
 * @brief Fold the @p len bytes at @p p into @p buf, upper-cased.
 *
 * One fold serves both questions asked of a word -- whether it is
 * reserved, and whether it is one of the few that change how the rest of
 * the line reads -- so the two can never disagree about what the word
 * was.  A word too long for any spelling folds to nothing, which is what
 * stops VFSWRITELONG from reading as VFSWRITE.
 */
static int
fold(const char *p, int len, char *buf, size_t max)
{
    int i;

    if (len <= 0 || (size_t)len >= max) {
        buf[0] = '\0';
        return 0;
    }
    for (i = 0; i < len; i++) {
        buf[i] = (char)up((unsigned char)p[i]);
    }
    buf[len] = '\0';
    return 1;
}

int
tiku_syntax_spans(tiku_syntax_lang_t lang, const char *line,
                  tiku_span_t *out, int max)
{
    spans_t s;
    int i = 0;
    int data_tail = 0;          /* past DATA: items, not statements     */
    int label_next = 0;         /* past GOTO/GOSUB: a target, not a word */
    int comma_arms = 0;         /* a comma here brings another target    */
    int at_head = 1;            /* nothing but blanks seen yet          */
    int at_num = 1;             /* the line's own number may still come  */
    int at_stmt = 1;            /* a statement may begin here            */

    if (line == NULL || out == NULL || max <= 0 ||
        lang != TIKU_SYNTAX_BASIC) {
        return 0;
    }
    s.out = out;
    s.max = max;
    s.n = 0;

    while (line[i] != '\0') {
        char c = line[i];
        int len;

        if (c == ' ' || c == '\t') {
            if (!push(&s, TIKU_INK_PLAIN, 1)) {
                break;
            }
            i++;
            continue;
        }
        if (at_num) {
            /*
             * A line may open with a number, and that number is the
             * line's NAME rather than a value in it -- so it stands back
             * with the gutter instead of reading as arithmetic.
             */
            at_num = 0;
            len = is_digit((unsigned char)c) ? number_at(line + i) : 0;
            if (len > 0) {
                if (!push(&s, TIKU_INK_FAINT, len)) {
                    break;
                }
                i += len;
                continue;       /* a label may still open the BODY */
            }
        }
        if (at_head) {
            /*
             * A label does the line-number's job by name, and it is a
             * name the person chose, so it keeps the document's own ink.
             * It may follow a line number -- `10 loop: PRINT 1` is a
             * numbered line whose body is labelled -- which is why the
             * number above does not close the head.
             */
            at_head = 0;
            len = label_at(line + i);
            if (len > 0) {
                if (!push(&s, TIKU_INK_PLAIN, len)) {
                    break;
                }
                i += len;
                continue;
            }
        }
        if (c == '"') {
            len = string_at(line + i);
            if (!push(&s, TIKU_INK_STR, len)) {
                break;
            }
            at_stmt = 0;
            comma_arms = 0;
            i += len;
            continue;
        }
        if (data_tail) {
            /*
             * After DATA the rest of the line is items, and the
             * interpreter never crunches it: a word there is a word, not
             * a statement.  Quoted items are still quoted (READ reads
             * them as strings), which is why the string test above comes
             * first.
             */
            if (!push(&s, TIKU_INK_PLAIN, 1)) {
                break;
            }
            i++;
            continue;
        }
        if (c == '\'' && at_stmt) {
            /*
             * To the end of the line, colons and all -- the interpreter
             * would not execute what follows, so it must not look
             * executable.  Only where a STATEMENT may begin, though:
             * mid-statement the interpreter does not take an apostrophe
             * as a comment at all, it refuses the line as trailing junk,
             * and colouring the tail as a remark would tell the reader
             * their line is fine when it will not run.
             */
            (void)push(&s, TIKU_INK_REMARK, (int)strlen(line + i));
            break;
        }
        if (c == '?') {
            /* The one reserved mark: `?` is PRINT spelled short. */
            if (!push(&s, TIKU_INK_WORD, 1)) {
                break;
            }
            at_stmt = 0;
            comma_arms = 0;
            i++;
            continue;
        }
        len = number_at(line + i);
        if (len > 0) {
            if (!push(&s, TIKU_INK_NUM, len)) {
                break;
            }
            if (label_next) {
                /* `GOTO 100` -- the target was a number, so the context
                 * is spent; a comma may bring another, anything else is
                 * ordinary code again. */
                label_next = 0;
                comma_arms = 1;
            } else {
                comma_arms = 0;
            }
            at_stmt = 0;
            i += len;
            continue;
        }
        len = word_at(line + i);
        if (len > 0) {
            char word[BASIC_WORD_MAX];
            int known = fold(line + i, len, word, sizeof word);
            int is_word;

            if (known && len > 1 && word[len - 1] == '$' &&
                !tiku_syntax_basic_word(word)) {
                /*
                 * The sigil does not have to belong to the word.  The
                 * interpreter matches a keyword and then asks only
                 * whether the next byte could CONTINUE a word -- and a
                 * `$` cannot -- so REM$ is REM followed by a `$`, and it
                 * starts a remark.  Only words the table has with the
                 * sigil (STR$, VFSREAD$) keep it.
                 */
                word[len - 1] = '\0';
                len--;
            }
            is_word = !label_next && known && tiku_syntax_basic_word(word);

            if (is_word && at_stmt && strcmp(word, "REM") == 0) {
                /* Like the apostrophe: everything after it, colons and
                 * all, is a remark the interpreter steps over. */
                (void)push(&s, TIKU_INK_REMARK, (int)strlen(line + i));
                break;
            }
            if (!push(&s, is_word ? TIKU_INK_WORD : TIKU_INK_PLAIN, len)) {
                break;
            }
            if (label_next) {
                /* This word WAS the target.  A comma may bring another
                 * one -- `GOTO l1, l2` -- but nothing else may, or the
                 * ELSE in `IF A THEN GOTO L1 ELSE ...` would be eaten as
                 * a target and stop reading as the keyword it is. */
                label_next = 0;
                comma_arms = 1;
            } else {
                comma_arms = 0;
                if (is_word) {
                    data_tail = (strcmp(word, "DATA") == 0);
                    /* A target may be a label -- a name the interpreter
                     * reads raw and never folds.  Colouring it reserved
                     * would be the lie. */
                    label_next = (strcmp(word, "GOTO") == 0 ||
                                  strcmp(word, "GOSUB") == 0);
                }
            }
            at_stmt = 0;
            i += len;
            continue;
        }
        if (c == ',') {
            if (comma_arms) {
                label_next = 1;
            }
            if (!push(&s, TIKU_INK_PLAIN, 1)) {
                break;
            }
            i++;
            continue;
        }
        comma_arms = 0;
        if (c == ':') {
            label_next = 0;     /* a new statement begins */
            at_stmt = 1;
        } else {
            at_stmt = 0;
        }
        if (!push(&s, TIKU_INK_PLAIN, 1)) {
            break;
        }
        i++;
    }
    return s.n;
}
