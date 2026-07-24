/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_token.inl - keyword crunching (A2).
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c
 * (before tiku_basic_lex.inl -- match_kw consults the table).
 *
 * Classic Microsoft-BASIC line crunching: when a numbered line is stored
 * (prog_store), every keyword in the table below folds to a single byte
 * 0x80+index; LIST / SAVE / TRACE detokenize back to canonical text.  The
 * interpreter's execution paths are DUAL: match_kw (and the line scanners)
 * accept either the token byte or the spelled-out keyword, so
 *
 *   - immediate-mode input (typed, never stored) runs as raw text,
 *   - stored program lines run crunched (keyword match = one byte compare,
 *     statement dispatch = a switch on the byte),
 *   - keywords NOT in the table (rare/large: networking, BLE, crypto, REPL
 *     commands) stay raw in stored lines and keep working via the text path.
 *
 * Crunch rules:
 *   - Only maximal identifier runs fold, and only on an exact, word-bounded
 *     match (PRINTER / TOTAL / FORI never fold -- same word-boundary rule
 *     match_kw itself applies, so semantics are unchanged).
 *   - A trailing '$' joins the word first (STR$ folds; A$ does not).
 *   - Nothing folds inside "..." string literals.
 *   - After REM (or the ' alias) and after DATA, the rest of the line is
 *     stored raw: comment text and DATA items are data, not keywords.
 *   - Outside those raw regions, stray bytes >= 0x80 in the input are
 *     replaced with '?', so in stored text a high byte IS a valid token.
 *
 * The on-media SAVE format stays detokenized (human-readable) text, so
 * programs saved before A2 load unchanged, and the checkpoint identity CRC
 * (computed over the crunched bytes) is stable across SAVE/LOAD because
 * crunching is deterministic.
 *
 * Known (pathological) limitation: a GOTO/GOSUB label spelled exactly like a
 * table keyword (e.g. `print:`) now folds and stops working as a label.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Char predicates are defined in tiku_basic_lex.inl (included after us);
 * same-TU forward declarations keep the include order simple. */
static char to_upper(char c);
static int  is_alpha(char c);
static int  is_word_cont(char c);

/*---------------------------------------------------------------------------*/
/* TOKEN TABLE                                                               */
/*---------------------------------------------------------------------------*/

/* X-macro keeps the enum and the spelling table in lockstep.  Entries the C
 * code references by name (REM/DATA raw-tail handling, the scanners, the
 * dispatch switch) come first; order is otherwise cosmetic. */
#define BASIC_TOK_LIST(X)                                                     \
    /* control flow + statements */                                           \
    X(REM,     "REM")     X(DATA,    "DATA")    X(IF,      "IF")              \
    X(THEN,    "THEN")    X(ELSE,    "ELSE")    X(ELSEIF,  "ELSEIF")          \
    X(ENDIF,   "ENDIF")   X(END,     "END")     X(SELECT,  "SELECT")          \
    X(CASE,    "CASE")    X(SUB,     "SUB")     X(ENDSUB,  "ENDSUB")          \
    X(CALL,    "CALL")    X(LOCAL,   "LOCAL")   X(RESULT,  "RESULT")          \
    X(FOR,     "FOR")     X(TO,      "TO")      X(STEP,    "STEP")            \
    X(NEXT,    "NEXT")    X(GOTO,    "GOTO")    X(GOSUB,   "GOSUB")           \
    X(RETURN,  "RETURN")  X(WHILE,   "WHILE")   X(WEND,    "WEND")            \
    X(REPEAT,  "REPEAT")  X(UNTIL,   "UNTIL")   X(EXIT,    "EXIT")            \
    X(CONTINUE,"CONTINUE")X(DIM,     "DIM")     X(READ,    "READ")            \
    X(RESTORE, "RESTORE") X(ON,      "ON")      X(CHANGE,  "CHANGE")          \
    X(ERROR,   "ERROR")   X(TIMER,   "TIMER")   X(EVERY,   "EVERY")           \
    X(PRINT,   "PRINT")   X(LET,     "LET")     X(CONST,   "CONST")           \
    X(INPUT,   "INPUT")   X(SWAP,    "SWAP")    X(DELAY,   "DELAY")           \
    X(SLEEP,   "SLEEP")   X(CLS,     "CLS")     X(TRACE,   "TRACE")           \
    X(RESUME,  "RESUME")  X(PERSIST, "PERSIST") X(USING,   "USING")           \
    X(DEF,     "DEF")     X(FN,      "FN")      X(STOP,    "STOP")            \
    X(OFF,     "OFF")                                                         \
    /* numeric expression keywords */                                         \
    X(AND,     "AND")     X(OR,      "OR")      X(XOR,     "XOR")             \
    X(NOT,     "NOT")     X(MOD,     "MOD")     X(ABS,     "ABS")             \
    X(INT,     "INT")     X(SGN,     "SGN")     X(MIN,     "MIN")             \
    X(MAX,     "MAX")     X(RND,     "RND")     X(SIN,     "SIN")             \
    X(COS,     "COS")     X(TAN,     "TAN")     X(SQR,     "SQR")             \
    X(FMUL,    "FMUL")    X(FDIV,    "FDIV")    X(FPOW,    "FPOW")            \
    X(SHL,     "SHL")     X(SHR,     "SHR")     X(LEN,     "LEN")             \
    X(VAL,     "VAL")     X(ASC,     "ASC")     X(ERR,     "ERR")             \
    X(ERL,     "ERL")     X(TRUE,    "TRUE")    X(FALSE,   "FALSE")           \
    X(PI,      "PI")      X(MILLIS,  "MILLIS")  X(SECS,    "SECS")            \
    X(INSTR,   "INSTR")                                                       \
    /* string functions + hardware words */                                   \
    X(STR_S,   "STR$")    X(CHR_S,   "CHR$")    X(HEX_S,   "HEX$")            \
    X(BIN_S,   "BIN$")    X(LEFT_S,  "LEFT$")   X(RIGHT_S, "RIGHT$")          \
    X(MID_S,   "MID$")    X(UPPER_S, "UPPER$")  X(LOWER_S, "LOWER$")          \
    X(TRIM_S,  "TRIM$")   X(INKEY_S, "INKEY$")  X(DATE_S,  "DATE$")           \
    X(TIME_S,  "TIME$")   X(FSTR_S,  "FSTR$")   X(VFSREAD, "VFSREAD")         \
    X(VFSWRITE,"VFSWRITE")X(PEEK,    "PEEK")    X(POKE,    "POKE")            \
    X(ADC,     "ADC")     X(PIN,     "PIN")     X(DIGREAD, "DIGREAD")         \
    X(DIGWRITE,"DIGWRITE")X(LED,     "LED")

enum {
#define X(id, s) BASIC_TOK_##id,
    BASIC_TOK_LIST(X)
#undef X
    BASIC_TOK_N
};

static const char *const basic_tok_tab[] = {
#define X(id, s) s,
    BASIC_TOK_LIST(X)
#undef X
};

#define BASIC_TOK_BASE      0x80u
#define BASIC_TOK_BYTE(id)  ((uint8_t)(BASIC_TOK_BASE + BASIC_TOK_##id))

_Static_assert(BASIC_TOK_N <= 128, "token bytes must fit 0x80..0xFF");

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/* Longest table keyword is 8 chars ("CONTINUE", "DIGWRITE", "VFSWRITE"). */
#define BASIC_TOK_KW_MAX 10u

/* Exact-match lookup of an UPPERCASE word; -1 if it is not a keyword. */
static int
basic_tok_find(const char *word)
{
    int i;
    for (i = 0; i < (int)BASIC_TOK_N; i++) {
        if (strcmp(basic_tok_tab[i], word) == 0) return i;
    }
    return -1;
}

/**
 * @brief Match keyword @p kw at @p t, accepting either its token byte or the
 *        spelled-out word-bounded text.
 *
 * The raw-text case assumes @p t sits at a word start (every caller scans
 * from a line start, after whitespace, or after a non-word byte).  @p kw is
 * an UPPERCASE table spelling.
 *
 * @return Bytes consumed on a match (1 for a token), 0 on no match.
 */
static size_t
tok_kw_at(const char *t, const char *kw)
{
    uint8_t b = (uint8_t)*t;
    if (b >= BASIC_TOK_BASE) {
        return (b < BASIC_TOK_BASE + BASIC_TOK_N &&
                strcmp(basic_tok_tab[b - BASIC_TOK_BASE], kw) == 0) ? 1u : 0u;
    }
    {
        const char *q = t;
        while (*kw) {
            if (to_upper(*q) != *kw) return 0;
            q++; kw++;
        }
        if (is_word_cont(*q)) return 0;
        return (size_t)(q - t);
    }
}

/*---------------------------------------------------------------------------*/
/* CRUNCH / DETOKENIZE                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Fold keywords in one program line to token bytes.
 *
 * Output is never longer than the input (tokens shrink), so @p cap ==
 * strlen(src)+1 always suffices; over-long input truncates safely.
 */
static void
basic_crunch(char *dst, size_t cap, const char *src)
{
    size_t      o          = 0;
    int         in_str     = 0;
    int         at_start   = 1;   /* at line start (label-definition position) */
    int         label_ref  = 0;   /* word right after GOTO/GOSUB is a label   */

    while (*src != '\0' && o + 1u < cap) {
        char c = *src;
        if (in_str) {
            dst[o++] = c;
            src++;
            if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            dst[o++] = c;
            src++;
            at_start = 0;
            continue;
        }
        if (c == '\'') {                     /* REM alias: rest is raw */
            while (*src != '\0' && o + 1u < cap) dst[o++] = *src++;
            break;
        }
        if (is_alpha(c)) {
            /* Maximal identifier run [+ optional '$'] -> fold iff the WHOLE
             * word is a keyword (word-bounded by construction). */
            const char *w = src;
            char        word[BASIC_TOK_KW_MAX + 2];
            size_t      wl = 0;
            int         tok = -1;
            int         used_dollar = 0;
            while (is_word_cont(*w)) w++;
            /* Labels stay raw text: a line-leading `name:` definition (the
             * run-loop's label skip matches exactly this shape) and the
             * word referenced by GOTO/GOSUB. */
            if ((at_start && *w == ':') || label_ref) {
                tok = -1;
            } else if (*w == '$') {
                /* word$: fold only the FULL `NAME$` spelling; never fold the
                 * bare prefix of a string identifier/function. */
                if ((size_t)(w - src) <= BASIC_TOK_KW_MAX) {
                    const char *r;
                    for (r = src; r < w; r++) word[wl++] = to_upper(*r);
                    word[wl]      = '$';
                    word[wl + 1u] = '\0';
                    tok = basic_tok_find(word);
                    if (tok >= 0) used_dollar = 1;
                }
            } else if ((size_t)(w - src) <= BASIC_TOK_KW_MAX) {
                const char *r;
                for (r = src; r < w; r++) word[wl++] = to_upper(*r);
                word[wl] = '\0';
                tok = basic_tok_find(word);
            }
            at_start  = 0;
            label_ref = 0;
            if (tok >= 0) {
                dst[o++] = (char)(BASIC_TOK_BASE + (unsigned)tok);
                src = w + (used_dollar ? 1 : 0);
                if (tok == BASIC_TOK_REM || tok == BASIC_TOK_DATA) {
                    /* comment text / DATA items are data -- store raw */
                    while (*src != '\0' && o + 1u < cap) dst[o++] = *src++;
                    break;
                }
                if (tok == BASIC_TOK_GOTO || tok == BASIC_TOK_GOSUB) {
                    label_ref = 1;           /* next word may be a label */
                }
                continue;
            }
            while (src < w && o + 1u < cap) dst[o++] = *src++;
            continue;
        }
        if (is_word_cont(c)) {               /* digit/underscore-led run */
            while (is_word_cont(*src) && o + 1u < cap) dst[o++] = *src++;
            at_start  = 0;
            label_ref = 0;
            continue;
        }
        if (c != ' ' && c != '\t') {
            at_start = 0;
            if (c != ',') label_ref = 0;     /* GOTO l1, l2 keeps label ctx */
        }
        /* Outside strings/comments a byte >= 0x80 would alias a token. */
        dst[o++] = ((uint8_t)c >= BASIC_TOK_BASE) ? '?' : c;
        src++;
    }
    dst[o] = '\0';
}

/**
 * @brief Expand token bytes back to canonical keyword text.
 *
 * Exact inverse of basic_crunch for crunched input (quote-aware; REM / DATA
 * raw tails copied verbatim).
 *
 * @return Number of bytes written (excluding the NUL), or -1 if @p cap was
 *         too small for the full expansion (dst still NUL-terminated).
 */
static int
basic_detok(char *dst, size_t cap, const char *src)
{
    size_t o      = 0;
    int    in_str = 0;
    int    raw    = 0;                       /* 1 after REM / DATA */

    while (*src != '\0') {
        uint8_t b = (uint8_t)*src;
        if (!in_str && !raw &&
            b >= BASIC_TOK_BASE && b < BASIC_TOK_BASE + BASIC_TOK_N) {
            const char *s = basic_tok_tab[b - BASIC_TOK_BASE];
            size_t      l = strlen(s);
            if (o + l + 1u > cap) break;
            memcpy(dst + o, s, l);
            o += l;
            src++;
            if (b == BASIC_TOK_BYTE(REM) || b == BASIC_TOK_BYTE(DATA)) raw = 1;
            continue;
        }
        if (o + 2u > cap) break;
        if (!raw && b == '"') in_str = !in_str;
        dst[o++] = (char)b;
        src++;
    }
    dst[o] = '\0';
    return (*src == '\0') ? (int)o : -1;
}

/* Stream a crunched line through SHELL_PRINTF without a detok buffer --
 * used by LIST and the TRACE echo (display is human-paced). */
static void
basic_detok_print(const char *src)
{
    int in_str = 0;
    int raw    = 0;
    while (*src != '\0') {
        uint8_t b = (uint8_t)*src;
        if (!in_str && !raw &&
            b >= BASIC_TOK_BASE && b < BASIC_TOK_BASE + BASIC_TOK_N) {
            SHELL_PRINTF("%s", basic_tok_tab[b - BASIC_TOK_BASE]);
            if (b == BASIC_TOK_BYTE(REM) || b == BASIC_TOK_BYTE(DATA)) raw = 1;
            src++;
            continue;
        }
        if (!raw && b == '"') in_str = !in_str;
        SHELL_PRINTF("%c", (char)b);
        src++;
    }
}
