/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore_ipc.h - LP SRAM layout shared by the P4 HP core and LP core.
 *
 * Included by both builds, so it stays C99 plus <stdint.h>.  A sender writes
 * the payload, then a fence, then the sequence or command word.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * LP SRAM, 0x50108000..0x50110000 (32 KB), from the bottom:
 *   0x50108000  vector table: 32 four-byte slots, mtvec
 *   0x50108080  reset entry (LP_SYS LP_CORE_BOOT_ADDR), then the image's
 *               .text, .rodata, .data and .bss
 *   0x5010e000  stack, 2 KB, growing down from 0x5010e800
 *   0x5010e800  tiku_lpcore_shared_t, 4 KB
 *   0x5010fa40  the HP firmware's 64-byte fault record
 *   0x5010fa80  LP ROM stack and data, which this tree leaves alone
 * tiku_lpcore.ld and tiku_lpcore_arch.c assert these bounds.
 */

#ifndef TIKU_LPCORE_IPC_H_
#define TIKU_LPCORE_IPC_H_

#include <stdint.h>

#define TIKU_LPCORE_RAM_BASE     0x50108000u
/** @brief Where the LP core starts: the reset entry after the vectors. */
#define TIKU_LPCORE_ENTRY        (TIKU_LPCORE_RAM_BASE + 0x80u)
/** @brief First byte past the image: the stack's floor. */
#define TIKU_LPCORE_IMAGE_END    0x5010e000u
#define TIKU_LPCORE_IMAGE_MAX    (TIKU_LPCORE_IMAGE_END - TIKU_LPCORE_RAM_BASE)
#define TIKU_LPCORE_STACK_TOP    0x5010e800u
#define TIKU_LPCORE_SHARED_ADDR  0x5010e800u
#define TIKU_LPCORE_SHARED_SIZE  0x1000u
/** @brief Start of the HP firmware's fault record, the page's upper bound. */
#define TIKU_LPCORE_FAULT_RECORD 0x5010fa40u

/**
 * @brief An LP SRAM address as a pointer on the core that runs this code.
 *
 * Both cores see LP SRAM at the same addresses; a host model defines this to
 * point into its own buffer.
 */
#ifndef TIKU_LPCORE_PTR
#define TIKU_LPCORE_PTR(a) ((void *)(uintptr_t)(a))
#endif

/** @brief Written to .magic once the payload reaches C. */
#define TIKU_LPCORE_MAGIC        0x4C50434Fu                 /* 'LPCO'      */
/** @brief Written to .magic by the trap handler after it records the trap. */
#define TIKU_LPCORE_MAGIC_FAULT  0x4C504321u                 /* 'LPC!'      */

/** @brief Bytes in each single-slot mailbox, one per direction. */
#define TIKU_LPCORE_MSG_CAP      240u

/** @brief Commands in .cmd; the payload clears .cmd when it takes one. */
#define TIKU_LPCORE_CMD_PARK     1u   /**< spin parked, answer RSP_PARKED     */
#define TIKU_LPCORE_CMD_RESUME   2u   /**< leave the park                     */
/** @brief Set the LED pad: arg[0] 0 off, 1 on, 2 blink every arg[1] passes;
 *         answer RSP_DONE. */
#define TIKU_LPCORE_CMD_LED      3u
/** @brief Probe I2C addresses 0x08..0x77 on SDA arg[0], SCL arg[1] (LP IOs);
 *         answer RSP_DONE with i2c_found filled. */
#define TIKU_LPCORE_CMD_I2C_SCAN 4u

/** @brief Wake the HP core: arg[0] 1 once arg[1] LP timer ticks have
 *         passed, 2 once LP IO arg[1] reads arg[2]; answer RSP_DONE once
 *         armed.  The wake fires once. */
#define TIKU_LPCORE_CMD_WAKE     5u

/** @brief Wake modes in arg[0] of TIKU_LPCORE_CMD_WAKE. */
#define TIKU_LPCORE_WAKE_TICKS   1u
#define TIKU_LPCORE_WAKE_IO      2u

/** @brief Answers in .rsp. */
#define TIKU_LPCORE_RSP_PARKED   1u
#define TIKU_LPCORE_RSP_DONE     2u
#define TIKU_LPCORE_RSP_BAD_ARG  3u

/** @brief The LP IO the FireBeetle 2 wires to its LED. */
#define TIKU_LPCORE_LED_IO       3u
/** @brief LP IOs are GPIO0..15. */
#define TIKU_LPCORE_LP_IO_COUNT  16u

/*---------------------------------------------------------------------------*/
/* LP agent: jobs the payload runs on every pass, the HP awake or asleep      */
/*---------------------------------------------------------------------------*/

#define TIKU_LPAGENT_JOBS        8u
#define TIKU_LPAGENT_SAMPLES     64u
/** @brief Bytes of HP command line kept per job, NUL included. */
#define TIKU_LPAGENT_CMD_MAX     32u

/** @brief Job kinds: what a run reads. */
#define TIKU_LPAGENT_KIND_IO     1u   /**< level of LP IO .source, 0 or 1 */
#define TIKU_LPAGENT_KIND_TIMER  2u   /**< the job's run count */

/** @brief Job operators.  A comparison fires when it turns true; CHANGE,
 *         RISE and FALL compare with the previous run; ALWAYS fires on
 *         every run. */
#define TIKU_LPAGENT_OP_EQ       1u
#define TIKU_LPAGENT_OP_NE       2u
#define TIKU_LPAGENT_OP_GT       3u
#define TIKU_LPAGENT_OP_LT       4u
#define TIKU_LPAGENT_OP_CHANGE   5u
#define TIKU_LPAGENT_OP_RISE     6u
#define TIKU_LPAGENT_OP_FALL     7u
#define TIKU_LPAGENT_OP_ALWAYS   8u

/** @brief Job actions, a bit set: wake the HP core (only while armed),
 *         append a sample.  Every firing counts in .hits. */
#define TIKU_LPAGENT_ACT_WAKE    1u
#define TIKU_LPAGENT_ACT_SAMPLE  2u

/** @brief Job .state bits, kept by the payload; the HP writes 0. */
#define TIKU_LPAGENT_SEEDED      1u   /**< .last holds a reading */
#define TIKU_LPAGENT_MATCHED     2u   /**< the comparison held last run */

/** @brief One job.  The HP clears .kind, writes the rest, then writes
 *         .kind; the payload skips a job whose .kind is 0. */
typedef struct {
    volatile uint8_t  kind;
    volatile uint8_t  source;       /* LP IO for KIND_IO                    */
    volatile uint8_t  op;
    volatile uint8_t  action;
    volatile uint32_t period;       /* LP timer ticks between runs, >= 1    */
    volatile int32_t  threshold;
    volatile uint32_t next_due;     /* LP timer low word of the next run    */
    volatile int32_t  last;         /* the previous run's value             */
    volatile uint32_t state;        /* TIKU_LPAGENT_SEEDED, _MATCHED        */
    volatile uint32_t hits;         /* firings since the job was written    */
} tiku_lpagent_job_t;

/** @brief One sample: when, which job, what it read. */
typedef struct {
    volatile uint32_t lp_ticks;     /* LP timer low word                    */
    volatile uint32_t job;
    volatile int32_t  value;
} tiku_lpagent_sample_t;

/** @brief The shared page at TIKU_LPCORE_SHARED_ADDR. */
typedef struct {
    volatile uint32_t magic;        /* TIKU_LPCORE_MAGIC once main() runs  */
    volatile uint32_t heartbeat;    /* advances while not parked           */
    volatile uint32_t cmd;          /* HP -> LP command word               */
    volatile uint32_t rsp;          /* LP -> HP answer word                */

    /* HP -> LP mailbox: buf and len first, then seq. */
    volatile uint32_t h2l_seq;
    volatile uint32_t h2l_len;
    volatile uint8_t  h2l_buf[TIKU_LPCORE_MSG_CAP];

    /* LP -> HP mailbox, the same order. */
    volatile uint32_t l2h_seq;
    volatile uint32_t l2h_len;
    volatile uint8_t  l2h_buf[TIKU_LPCORE_MSG_CAP];

    /* Entries into main() since the image was loaded: one per launch. */
    volatile uint32_t boots;

    /* Command arguments, written before .cmd. */
    volatile uint32_t arg[4];

    /* Published every pass: the core's mcycle and the LP timer's count,
     * latched by the payload. */
    volatile uint32_t cycles;
    volatile uint32_t lp_ticks_lo;
    volatile uint32_t lp_ticks_hi;

    /* LED pad state the payload drives: 0 off, 1 on. */
    volatile uint32_t led_level;

    /* I2C scan result: bit (a & 7) of i2c_found[a >> 3] set when address
     * a acknowledged; i2c_count of them; i2c_scl_stuck set when SCL never
     * rose. */
    volatile uint8_t  i2c_found[16];
    volatile uint32_t i2c_count;
    volatile uint32_t i2c_scl_stuck;

    /* HP wake: the armed mode (0 when none), wakes sent, and the LP timer's
     * low word when the latest one was sent. */
    volatile uint32_t wake_mode;
    volatile uint32_t wake_count;
    volatile uint32_t wake_lp_ticks;

    /* LP agent.  agent_armed is set by the HP before a deep sleep; a job's
     * wake fires only while it is set, records itself and clears it.  The
     * ring holds the latest TIKU_LPAGENT_SAMPLES of sample_count samples.
     * agent_cmd is the HP's: the command line to run after each job's
     * wake. */
    volatile uint32_t agent_armed;
    volatile uint32_t agent_wake_job;    /* job + 1 that woke the HP, or 0  */
    volatile int32_t  agent_wake_value;
    volatile uint32_t agent_wake_ticks;
    volatile uint32_t sample_count;
    tiku_lpagent_job_t    jobs[TIKU_LPAGENT_JOBS];
    tiku_lpagent_sample_t samples[TIKU_LPAGENT_SAMPLES];
    volatile char         agent_cmd[TIKU_LPAGENT_JOBS][TIKU_LPAGENT_CMD_MAX];

    /* Fault record, written by the trap handler before it sets the fault
     * magic.  fault_count survives relaunches; the rest describe the most
     * recent trap. */
    volatile uint32_t fault_count;
    volatile uint32_t fault_cause;  /* mcause */
    volatile uint32_t fault_epc;    /* mepc   */
    volatile uint32_t fault_tval;   /* mtval  */
} tiku_lpcore_shared_t;

#define TIKU_LPCORE_SHARED \
    ((tiku_lpcore_shared_t *)TIKU_LPCORE_PTR(TIKU_LPCORE_SHARED_ADDR))

_Static_assert(sizeof(tiku_lpcore_shared_t) <= TIKU_LPCORE_SHARED_SIZE,
               "lpcore: the shared page outgrew its 4 KB");
_Static_assert(TIKU_LPCORE_SHARED_ADDR + TIKU_LPCORE_SHARED_SIZE <=
               TIKU_LPCORE_FAULT_RECORD,
               "lpcore: the shared page overlaps the HP fault record");

#endif /* TIKU_LPCORE_IPC_H_ */
