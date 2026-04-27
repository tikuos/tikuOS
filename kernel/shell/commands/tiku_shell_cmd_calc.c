/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_calc.c - "calc" command implementation
 *
 * Evaluates a space-separated infix expression of integer literals
 * combined by +, -, *, /, %, min, max.  Arithmetic is 32-bit signed;
 * no floating point (the FR5969 has no FPU and softfloat would dwarf
 * the rest of the shell).
 *
 * Strategy: a fixed-size operand/operator stack (four operands, three
 * operators -- the most that fits in the eight-token argv limit) is
 * reduced in two passes.  Pass 1 collapses every high-precedence
 * operator (*, /, %, min, max) left-to-right; pass 2 collapses the
 * remaining additive operators.  This matches schoolbook precedence
 * for the arithmetic ops without needing a full Pratt or Shunting-yard
 * parser.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_calc.h"
#include <kernel/shell/tiku_shell.h>
#include <limits.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* TYPES & CONSTANTS                                                         */
/*---------------------------------------------------------------------------*/

typedef enum {
    CALC_OP_ADD,
    CALC_OP_SUB,
    CALC_OP_MUL,
    CALC_OP_DIV,
    CALC_OP_MOD,
    CALC_OP_MIN,
    CALC_OP_MAX
} calc_op_t;

/* Bounded by the parser's TIKU_SHELL_MAX_ARGS (8): "calc" + 7 expr
 * tokens = at most 4 operands and 3 operators. */
#define CALC_MAX_NUMS  4
#define CALC_MAX_OPS   (CALC_MAX_NUMS - 1)

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief True when @p op binds tighter than + and -.
 */
static uint8_t
calc_op_is_high(calc_op_t op)
{
    return op != CALC_OP_ADD && op != CALC_OP_SUB;
}

/**
 * @brief Strict signed-decimal parse with overflow detection.
 *
 * Accepts an optional leading '+' or '-'; rejects empty strings, any
 * non-digit body character, and values outside the long range.
 *
 * @return 1 on success (value written to *out), 0 on parse error.
 */
static uint8_t
calc_parse_long(const char *s, long *out)
{
    long val = 0;
    uint8_t neg = 0;

    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (*s == '\0') {
        return 0;
    }
    while (*s != '\0') {
        long digit;
        if (*s < '0' || *s > '9') {
            return 0;
        }
        digit = (long)(*s - '0');
        if (val > (LONG_MAX - digit) / 10) {
            return 0;
        }
        val = val * 10 + digit;
        s++;
    }
    *out = neg ? -val : val;
    return 1;
}

/**
 * @brief Map a token to a calc_op_t.
 * @return 1 on success, 0 if the token is not a known operator.
 */
static uint8_t
calc_parse_op(const char *s, calc_op_t *out)
{
    if (s[0] != '\0' && s[1] == '\0') {
        switch (s[0]) {
        case '+': *out = CALC_OP_ADD; return 1;
        case '-': *out = CALC_OP_SUB; return 1;
        case '*': *out = CALC_OP_MUL; return 1;
        case '/': *out = CALC_OP_DIV; return 1;
        case '%': *out = CALC_OP_MOD; return 1;
        }
    }
    if (strcmp(s, "min") == 0) {
        *out = CALC_OP_MIN;
        return 1;
    }
    if (strcmp(s, "max") == 0) {
        *out = CALC_OP_MAX;
        return 1;
    }
    return 0;
}

/**
 * @brief Apply a single binary operator with error reporting.
 *
 * Division and modulo by zero print an error and return 0; all other
 * operations succeed (signed overflow wraps as on the underlying ALU).
 *
 * @return 1 on success, 0 on error (message already printed).
 */
static uint8_t
calc_apply_op(long a, calc_op_t op, long b, long *out)
{
    switch (op) {
    case CALC_OP_ADD:
        *out = a + b;
        return 1;
    case CALC_OP_SUB:
        *out = a - b;
        return 1;
    case CALC_OP_MUL:
        *out = a * b;
        return 1;
    case CALC_OP_DIV:
        if (b == 0) {
            SHELL_PRINTF("calc: divide by zero\n");
            return 0;
        }
        *out = a / b;
        return 1;
    case CALC_OP_MOD:
        if (b == 0) {
            SHELL_PRINTF("calc: modulo by zero\n");
            return 0;
        }
        *out = a % b;
        return 1;
    case CALC_OP_MIN:
        *out = (a < b) ? a : b;
        return 1;
    case CALC_OP_MAX:
        *out = (a > b) ? a : b;
        return 1;
    }
    return 0;
}

/**
 * @brief Collapse one position in the (nums, ops) sequence in place.
 *
 * Replaces nums[i],ops[i],nums[i+1] with the result of the binary op,
 * shifting everything to the right of i down by one slot.
 *
 * @return 1 on success, 0 if the operator itself failed (e.g., div0).
 */
static uint8_t
calc_reduce_at(long *nums, calc_op_t *ops, uint8_t i,
               uint8_t *n_nums, uint8_t *n_ops)
{
    long r;
    uint8_t j;

    if (!calc_apply_op(nums[i], ops[i], nums[i + 1], &r)) {
        return 0;
    }
    nums[i] = r;
    for (j = (uint8_t)(i + 1); j + 1 < *n_nums; j++) {
        nums[j] = nums[j + 1];
    }
    for (j = i; j + 1 < *n_ops; j++) {
        ops[j] = ops[j + 1];
    }
    (*n_nums)--;
    (*n_ops)--;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_calc(uint8_t argc, const char *argv[])
{
    long      nums[CALC_MAX_NUMS];
    calc_op_t ops[CALC_MAX_OPS];
    uint8_t   n_nums;
    uint8_t   n_ops;
    uint8_t   i;

    /* Expression layout: NUM (OP NUM)*  -> argc-1 must be odd, argc even. */
    if (argc < 2 || (argc & 1) != 0) {
        SHELL_PRINTF("Usage: calc N [op N]...\n");
        SHELL_PRINTF("Ops: + - * / %% min max  (32-bit signed, no float)\n");
        return;
    }

    n_ops  = (uint8_t)((argc - 2) / 2);
    n_nums = (uint8_t)(n_ops + 1);

    /* The argv limit (TIKU_SHELL_MAX_ARGS = 8) caps argc at 8, so
     * n_nums cannot exceed CALC_MAX_NUMS.  Belt-and-suspenders: */
    if (n_nums > CALC_MAX_NUMS) {
        SHELL_PRINTF("calc: expression too long\n");
        return;
    }

    /* Parse operands at odd argv positions (1, 3, 5, 7) and operators
     * at even positions (2, 4, 6). */
    for (i = 0; i < n_nums; i++) {
        if (!calc_parse_long(argv[1 + 2 * i], &nums[i])) {
            SHELL_PRINTF("calc: not a number: '%s'\n", argv[1 + 2 * i]);
            return;
        }
    }
    for (i = 0; i < n_ops; i++) {
        if (!calc_parse_op(argv[2 + 2 * i], &ops[i])) {
            SHELL_PRINTF("calc: unknown operator: '%s'\n", argv[2 + 2 * i]);
            return;
        }
    }

    /* Pass 1: reduce *, /, %, min, max left-to-right. */
    i = 0;
    while (i < n_ops) {
        if (calc_op_is_high(ops[i])) {
            if (!calc_reduce_at(nums, ops, i, &n_nums, &n_ops)) {
                return;
            }
        } else {
            i++;
        }
    }

    /* Pass 2: reduce remaining + and - left-to-right. */
    while (n_ops > 0) {
        if (!calc_reduce_at(nums, ops, 0, &n_nums, &n_ops)) {
            return;
        }
    }

    SHELL_PRINTF("  %ld\n", nums[0]);
}
