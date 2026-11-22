/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_thread.c - threads, mutexes and condition variables on kernel
 * workers and wait queues, behind 32-bit handles; tiku_posix_pthread.c
 * gives them the C library's pthread names.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "tiku_posix.h"
#include <hal/tiku_cpu.h>
#include <kernel/threads/tiku_thread.h>

/* A handle of 0 or 0xFFFFFFFF (the C library's static initialiser) has no
 * object yet; one is taken from the table at first use.  Others are an
 * index plus one. */
#define UNSET(h) ((h) == 0u || (h) == 0xFFFFFFFFu)

/* The kernel thread's identity: it has no control block. */
#define KERNEL_ID 1u

typedef struct {
    uint8_t used;
    uint8_t type;               /* TIKU_POSIX_MUTEX_* */
    uint16_t depth;             /* a recursive mutex's holds */
    uint32_t owner;             /* holder's identity, 0 when free */
    tiku_waitq_t waiters;
} posix_mutex_t;

typedef struct {
    uint8_t used;
    uint32_t seq;               /* signals so far: a waiter returns when it moves */
    tiku_waitq_t waiters;
} posix_cond_t;

typedef struct {
    tiku_thread_t tcb;          /* first: its address is the thread's identity */
    uint8_t used;               /* created, and not yet joined or detached-done */
    uint8_t detached;
    volatile uint8_t finished;
    void *(*fn)(void *);
    void *arg;
    void *result;
    tiku_waitq_t joiners;
} posix_thread_t;

_Static_assert(sizeof(posix_mutex_t) <= sizeof(tiku_posix_lock_t),
               "a lock holds a mutex");
_Static_assert(_Alignof(posix_mutex_t) <= _Alignof(tiku_posix_lock_t),
               "a lock is aligned for a mutex");

static posix_mutex_t mutexes[TIKU_POSIX_MUTEXES];
static posix_cond_t conds[TIKU_POSIX_CONDS];
static posix_thread_t threads[TIKU_POSIX_THREADS];
static uint8_t *stack_memory;
static size_t stack_each;
static unsigned refused;        /* objects not taken: their table was full */

uint32_t tiku_posix_self(void)
{
    tiku_thread_t *self = tiku_thread_self();

    return self == NULL ? KERNEL_ID : (uint32_t)(uintptr_t)self;
}

/*---------------------------------------------------------------------------*/
/* MUTEXES                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief @p h's object, taking one for an unset handle; NULL when the
 *         table is full or the handle names no live object.
 *  @note Inside an atomic section. */
static posix_mutex_t *mutex_of(uint32_t *h, int type)
{
    if (UNSET(*h)) {
        for (unsigned i = 0; i < TIKU_POSIX_MUTEXES; i++) {
            if (!mutexes[i].used) {
                memset(&mutexes[i], 0, sizeof mutexes[i]);
                mutexes[i].used = 1u;
                mutexes[i].type = (uint8_t)type;
                *h = i + 1u;
                return &mutexes[i];
            }
        }
        refused++;
        return NULL;
    }
    if (*h > TIKU_POSIX_MUTEXES || !mutexes[*h - 1u].used) return NULL;
    return &mutexes[*h - 1u];
}

int tiku_posix_mutex_init(uint32_t *h, int type)
{
    int r = 0;

    if (h == NULL || type < TIKU_POSIX_MUTEX_NORMAL || type > TIKU_POSIX_MUTEX_ERRORCHECK)
        return EINVAL;
    tiku_atomic_enter();
    *h = 0xFFFFFFFFu;
    if (mutex_of(h, type) == NULL) r = EAGAIN;
    tiku_atomic_exit();
    return r;
}

int tiku_posix_mutex_destroy(uint32_t *h)
{
    posix_mutex_t *m;
    int r = 0;

    if (h == NULL) return EINVAL;
    tiku_atomic_enter();
    if (!UNSET(*h)) {
        m = mutex_of(h, TIKU_POSIX_MUTEX_NORMAL);
        if (m == NULL) r = EINVAL;
        else if (m->owner != 0u || m->waiters.waiters != 0u) r = EBUSY;
        else {
            m->used = 0u;
            *h = 0xFFFFFFFFu;
        }
    }
    tiku_atomic_exit();
    return r;
}

/** @brief One attempt by @p self: 0 taken, EBUSY held by another, or an
 *         error.  @note Inside an atomic section. */
static int mutex_take(posix_mutex_t *m, uint32_t self)
{
    if (m->owner == 0u) {
        m->owner = self;
        m->depth = 1u;
        return 0;
    }
    if (m->owner != self) return EBUSY;
    if (m->type == TIKU_POSIX_MUTEX_RECURSIVE) {
        if (m->depth == UINT16_MAX) return EAGAIN;
        m->depth++;
        return 0;
    }
    return EDEADLK;
}

/** @brief Take @p m for @p self, waiting on a worker while another holds
 *         it unless @p try_only.  The kernel thread does not wait: a held
 *         mutex is EAGAIN there.  @note Inside an atomic section. */
static int mutex_take_waiting(posix_mutex_t *m, uint32_t self, int try_only)
{
    int r = mutex_take(m, self);

    while (r == EBUSY && !try_only && self != KERNEL_ID) {
        (void)tiku_thread_wait(&m->waiters, 0u);
        r = mutex_take(m, self);
    }
    return (r == EBUSY && !try_only) ? EAGAIN : r;
}

int tiku_posix_mutex_lock(uint32_t *h, int try_only)
{
    uint32_t self = tiku_posix_self();
    posix_mutex_t *m;
    int r;

    if (h == NULL) return EINVAL;
    tiku_atomic_enter();
    m = mutex_of(h, TIKU_POSIX_MUTEX_NORMAL);
    if (m == NULL) {
        tiku_atomic_exit();
        return UNSET(*h) ? EAGAIN : EINVAL;
    }
    r = mutex_take_waiting(m, self, try_only);
    tiku_atomic_exit();
    return r;
}

/** @brief Release @p m held by @p self.  @note Inside an atomic section. */
static int mutex_give(posix_mutex_t *m, uint32_t self)
{
    if (m->owner != self) return EPERM;
    if (--m->depth == 0u) {
        m->owner = 0u;
        tiku_thread_wake_one(&m->waiters);
    }
    return 0;
}

int tiku_posix_mutex_unlock(uint32_t *h)
{
    posix_mutex_t *m;
    int r;

    if (h == NULL || UNSET(*h)) return EPERM;
    tiku_atomic_enter();
    m = mutex_of(h, TIKU_POSIX_MUTEX_NORMAL);
    r = m == NULL ? EINVAL : mutex_give(m, tiku_posix_self());
    tiku_atomic_exit();
    return r;
}

void tiku_posix_thread_use(unsigned *mutexes_used, unsigned *conds_used, unsigned *refusals)
{
    unsigned m = 0, c = 0;

    tiku_atomic_enter();
    for (unsigned i = 0; i < TIKU_POSIX_MUTEXES; i++) m += mutexes[i].used;
    for (unsigned i = 0; i < TIKU_POSIX_CONDS; i++) c += conds[i].used;
    if (mutexes_used != NULL) *mutexes_used = m;
    if (conds_used != NULL) *conds_used = c;
    if (refusals != NULL) *refusals = refused;
    tiku_atomic_exit();
}

int tiku_posix_lock_acquire(tiku_posix_lock_t *lock, int try_only)
{
    posix_mutex_t *m = (posix_mutex_t *)(void *)lock;
    int r;

    if (lock == NULL) return EINVAL;
    tiku_atomic_enter();
    m->type = TIKU_POSIX_MUTEX_RECURSIVE;
    r = mutex_take_waiting(m, tiku_posix_self(), try_only);
    tiku_atomic_exit();
    return r;
}

int tiku_posix_lock_release(tiku_posix_lock_t *lock)
{
    int r;

    if (lock == NULL) return EINVAL;
    tiku_atomic_enter();
    r = mutex_give((posix_mutex_t *)(void *)lock, tiku_posix_self());
    tiku_atomic_exit();
    return r;
}

/*---------------------------------------------------------------------------*/
/* CONDITION VARIABLES                                                       */
/*---------------------------------------------------------------------------*/

static posix_cond_t *cond_of(uint32_t *h)
{
    if (UNSET(*h)) {
        for (unsigned i = 0; i < TIKU_POSIX_CONDS; i++) {
            if (!conds[i].used) {
                memset(&conds[i], 0, sizeof conds[i]);
                conds[i].used = 1u;
                *h = i + 1u;
                return &conds[i];
            }
        }
        refused++;
        return NULL;
    }
    if (*h > TIKU_POSIX_CONDS || !conds[*h - 1u].used) return NULL;
    return &conds[*h - 1u];
}

int tiku_posix_cond_init(uint32_t *h)
{
    int r = 0;

    if (h == NULL) return EINVAL;
    tiku_atomic_enter();
    *h = 0xFFFFFFFFu;
    if (cond_of(h) == NULL) r = EAGAIN;
    tiku_atomic_exit();
    return r;
}

int tiku_posix_cond_destroy(uint32_t *h)
{
    posix_cond_t *c;
    int r = 0;

    if (h == NULL) return EINVAL;
    tiku_atomic_enter();
    if (!UNSET(*h)) {
        c = cond_of(h);
        if (c == NULL) r = EINVAL;
        else if (c->waiters.waiters != 0u) r = EBUSY;
        else {
            c->used = 0u;
            *h = 0xFFFFFFFFu;
        }
    }
    tiku_atomic_exit();
    return r;
}

int tiku_posix_cond_wait(uint32_t *ch, uint32_t *mh)
{
    uint32_t self = tiku_posix_self(), seq;
    posix_cond_t *c;
    posix_mutex_t *m;
    int r;

    if (ch == NULL || mh == NULL) return EINVAL;
    if (self == KERNEL_ID) return EAGAIN;      /* the kernel thread does not wait */
    tiku_atomic_enter();
    c = cond_of(ch);
    m = UNSET(*mh) ? NULL : mutex_of(mh, TIKU_POSIX_MUTEX_NORMAL);
    if (c == NULL || m == NULL) {
        tiku_atomic_exit();
        return (c == NULL && UNSET(*ch)) ? EAGAIN : EINVAL;
    }
    if (m->owner != self || m->depth != 1u) {
        tiku_atomic_exit();
        return EPERM;
    }
    /* Releasing the mutex and starting to wait are one step: a signal
     * between them moves seq, which the loop sees. */
    seq = c->seq;
    (void)mutex_give(m, self);
    while (c->seq == seq) {
        (void)tiku_thread_wait(&c->waiters, 0u);
    }
    r = mutex_take(m, self);
    while (r == EBUSY) {
        (void)tiku_thread_wait(&m->waiters, 0u);
        r = mutex_take(m, self);
    }
    tiku_atomic_exit();
    return r;
}

int tiku_posix_cond_signal(uint32_t *h, int all)
{
    posix_cond_t *c;

    if (h == NULL) return EINVAL;
    tiku_atomic_enter();
    c = cond_of(h);
    if (c != NULL) {
        c->seq++;
        if (all) tiku_thread_wake_all(&c->waiters);
        else tiku_thread_wake_one(&c->waiters);
    }
    tiku_atomic_exit();
    return c == NULL ? EINVAL : 0;
}

/*---------------------------------------------------------------------------*/
/* THREADS                                                                   */
/*---------------------------------------------------------------------------*/

int tiku_posix_thread_memory(void *base, size_t bytes)
{
    uintptr_t at = ((uintptr_t)base + 15u) & ~(uintptr_t)15u;
    size_t each;

    if (base == NULL || bytes < (at - (uintptr_t)base)) return EINVAL;
    each = ((bytes - (at - (uintptr_t)base)) / TIKU_POSIX_THREADS) & ~(size_t)15u;
    if (each < TIKU_THREAD_STACK_MIN) return EINVAL;
    tiku_atomic_enter();
    for (unsigned i = 0; i < TIKU_POSIX_THREADS; i++) {
        if (threads[i].used) {
            tiku_atomic_exit();
            return EBUSY;
        }
    }
    stack_memory = (uint8_t *)at;
    stack_each = each;
    tiku_atomic_exit();
    return 0;
}

/** @brief A finished thread's record: its result, its joiners woken, and its
 *         slot freed when detached.  The control block is reused only after
 *         the kernel has marked it done. */
static void thread_finish(posix_thread_t *t, void *result)
{
    tiku_atomic_enter();
    t->result = result;
    t->finished = 1u;
    if (t->detached) t->used = 0u;
    tiku_thread_wake_all(&t->joiners);
    tiku_atomic_exit();
}

static void thread_main(void *arg)
{
    posix_thread_t *t = arg;

    thread_finish(t, t->fn(t->arg));
}

struct create_call {
    uint32_t *id;
    size_t stack;
    int detached;
    void *(*fn)(void *);
    void *arg;
};

/* On the kernel thread: tiku_thread_start() is the kernel's to call. */
static int create_on_kernel(void *arg)
{
    struct create_call *c = arg;
    posix_thread_t *t = NULL;
    unsigned i;

    if (stack_memory == NULL || (c->stack != 0u && c->stack > stack_each)) return EAGAIN;
    for (i = 0; i < TIKU_POSIX_THREADS; i++) {
        if (!threads[i].used && (threads[i].tcb.state == TIKU_THREAD_UNUSED ||
                                 threads[i].tcb.state == TIKU_THREAD_DONE)) {
            t = &threads[i];
            break;
        }
    }
    if (t == NULL) return EAGAIN;
    (void)tiku_thread_forget(&t->tcb);
    memset(t, 0, sizeof *t);
    t->tcb.stack_base = (uint32_t *)(void *)(stack_memory + i * stack_each);
    t->tcb.stack_size = stack_each;
    t->tcb.name = "posix";
    t->fn = c->fn;
    t->arg = c->arg;
    t->detached = (uint8_t)(c->detached != 0);
    t->used = 1u;
    if (tiku_thread_start(&t->tcb, thread_main, t) != 0) {
        t->used = 0u;
        return EAGAIN;
    }
    *c->id = (uint32_t)(uintptr_t)&t->tcb;
    return 0;
}

int tiku_posix_thread_create(uint32_t *id, size_t stack, int detached,
                             void *(*fn)(void *), void *arg)
{
    struct create_call c;

    if (id == NULL || fn == NULL) return EINVAL;
    c.id = id;
    c.stack = stack;
    c.detached = detached;
    c.fn = fn;
    c.arg = arg;
    return tiku_thread_call(create_on_kernel, &c);
}

static posix_thread_t *thread_of(uint32_t id)
{
    for (unsigned i = 0; i < TIKU_POSIX_THREADS; i++) {
        if ((uint32_t)(uintptr_t)&threads[i].tcb == id && threads[i].used) return &threads[i];
    }
    return NULL;
}

int tiku_posix_thread_join(uint32_t id, void **result)
{
    posix_thread_t *t;
    int r = 0;

    if (id == tiku_posix_self()) return EDEADLK;
    tiku_atomic_enter();
    t = thread_of(id);
    if (t == NULL || t->detached) r = (t == NULL) ? ESRCH : EINVAL;
    else if (!t->finished && tiku_posix_self() == KERNEL_ID) r = EAGAIN;
    else {
        while (!t->finished) {
            (void)tiku_thread_wait(&t->joiners, 0u);
        }
        if (result != NULL) *result = t->result;
        t->used = 0u;
    }
    tiku_atomic_exit();
    return r;
}

int tiku_posix_thread_detach(uint32_t id)
{
    posix_thread_t *t;
    int r = 0;

    tiku_atomic_enter();
    t = thread_of(id);
    if (t == NULL) r = ESRCH;
    else if (t->detached) r = EINVAL;
    else {
        t->detached = 1u;
        if (t->finished) t->used = 0u;
    }
    tiku_atomic_exit();
    return r;
}

void tiku_posix_thread_exit(void *result)
{
    tiku_thread_t *self = tiku_thread_self();

    for (unsigned i = 0; i < TIKU_POSIX_THREADS; i++) {
        if (self != NULL && &threads[i].tcb == self) {
            thread_finish(&threads[i], result);
            break;
        }
    }
    tiku_thread_exit();
}
