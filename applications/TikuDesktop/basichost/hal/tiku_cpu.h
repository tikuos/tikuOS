#ifndef TIKU_HOST_CPU_STUB_H_
#define TIKU_HOST_CPU_STUB_H_
/* A host has no low-power idle to enter; the SHAPE survives so the
 * interpreter's SLEEP path compiles unchanged, and the NULL it gets
 * back means "nothing to call between wakes", which is true here. */
typedef void (*tiku_cpu_idle_enter_t)(void);
typedef int tiku_cpu_idle_mode_t;
#define TIKU_CPU_IDLE_DEEP 1
static inline tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t m)
{
    (void)m;
    return (tiku_cpu_idle_enter_t)0;
}
static inline void tiku_cpu_icache_invalidate(void) {}
#endif
