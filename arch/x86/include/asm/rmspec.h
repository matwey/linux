#ifndef _LINUX_RMSPEC_H
#define _LINUX_RMSPEC_H
#include <asm/msr.h>
#include <asm/spec-ctrl.h>

/*
 * We call these when we *know* the CPU can go in/out of its
 * "safer" reduced memory speculation mode.
 *
 * For BPF, x86_sync_spec_ctrl() reads the per-cpu BPF state
 * variable and figures out the MSR value by itself.  Thus,
 * we do not need to pass the "direction".
 */
static inline void cpu_enter_reduced_memory_speculation(void)
{
	x86_sync_spec_ctrl();
}

static inline void cpu_leave_reduced_memory_speculation(void)
{
	x86_sync_spec_ctrl();
}

#endif /* _LINUX_RMSPEC_H */
