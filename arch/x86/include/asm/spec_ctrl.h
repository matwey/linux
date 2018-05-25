#ifndef _ASM_X86_SPEC_CTRL_H
#define _ASM_X86_SPEC_CTRL_H

#include <linux/stringify.h>
#include <asm/msr-index.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>

#ifdef __ASSEMBLY__

.macro __ENABLE_IBRS_CLOBBER
	movl $MSR_IA32_SPEC_CTRL, %ecx
	rdmsr
	orl $FEATURE_ENABLE_IBRS, %eax
	wrmsr
.endm

.macro ENABLE_IBRS_CLOBBER
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_SPEC_CTRL
	call x86_ibrs_enabled
	test %eax, %eax
	jz .Llfence_\@

	__ENABLE_IBRS_CLOBBER
	jmp .Lend_\@

.Llfence_\@:
	lfence
.Lend_\@:
.endm


.macro ENABLE_IBRS
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_SPEC_CTRL

	pushq %rax

	call x86_ibrs_enabled
	test %eax, %eax
	jz .Llfence_\@

	pushq %rcx
	pushq %rdx
	__ENABLE_IBRS_CLOBBER
	popq %rdx
	popq %rcx

	jmp .Lpop_\@

.Llfence_\@:
	lfence

.Lpop_\@:
	popq %rax

.Lend_\@:
.endm


.macro DISABLE_IBRS
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_SPEC_CTRL

	pushq %rax

	call x86_ibrs_enabled
	test %eax, %eax
	jz .Llfence_\@

	pushq %rcx
	pushq %rdx
	movl $MSR_IA32_SPEC_CTRL, %ecx
	rdmsr
	xorl $FEATURE_ENABLE_IBRS, %eax
	wrmsr
	popq %rdx
	popq %rcx

	jmp .Lpop_\@

.Llfence_\@:
	lfence

.Lpop_\@:
	popq %rax

.Lend_\@:
.endm

#else /* __ASSEMBLY__ */
#include <linux/thread_info.h>

extern int ibrs_state;
void x86_enable_ibrs(void);
void x86_disable_ibrs(void);
unsigned int x86_ibrs_enabled(void);
unsigned int x86_ibpb_enabled(void);
void x86_spec_check(void);
int nospec(char *str);

static inline void x86_ibp_barrier(void)
{
	if (x86_ibpb_enabled())
		native_wrmsrl(MSR_IA32_PRED_CMD, FEATURE_SET_IBPB);
}

/*
 * On VMENTER we must preserve whatever view of the SPEC_CTRL MSR
 * the guest has, while on VMEXIT we restore the host view. This
 * would be easier if SPEC_CTRL were architecturally maskable or
 * shadowable for guests but this is not (currently) the case.
 * Takes the guest view of SPEC_CTRL MSR as a parameter.
 */
extern void x86_spec_ctrl_set_guest(u64);
extern void x86_spec_ctrl_restore_host(u64);

/* AMD specific Speculative Store Bypass MSR data */
extern u64 x86_amd_ls_cfg_base;
extern u64 x86_amd_ls_cfg_ssbd_mask;

/* The Intel SPEC CTRL MSR base value cache */
extern u64 x86_spec_ctrl_base;

static inline u64 ssbd_tif_to_spec_ctrl(u64 tifn)
{
	BUILD_BUG_ON(TIF_SSBD < SPEC_CTRL_SSBD_SHIFT);
	return (tifn & _TIF_SSBD) >> (TIF_SSBD - SPEC_CTRL_SSBD_SHIFT);
}

static inline u64 ssbd_tif_to_amd_ls_cfg(u64 tifn)
{
	return (tifn & _TIF_SSBD) ? x86_amd_ls_cfg_ssbd_mask : 0ULL;
}

#ifdef CONFIG_SMP
extern void speculative_store_bypass_ht_init(void);
#else
static inline void speculative_store_bypass_ht_init(void) { }
#endif

extern void speculative_store_bypass_update(void);

#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_SPEC_CTRL_H */
