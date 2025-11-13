.global default_trap, timer_apic, task_init, task_start
.code64

/*
 * These macros save and restore volatile registers
 * (assuming you do not modify any other registers except
 * as used by the C calling convention).
 */
#define SAVE_REGS_NORAX				 \
	pushq %rcx						;\
	pushq %rdx						;\
	pushq %rdi						;\
	pushq %rsi						;\
	pushq %r8						;\
	pushq %r9						;\
	pushq %r10						;\
	pushq %r11

#define SAVE_REGS					\
	pushq %rax						;\
	SAVE_REGS_NORAX

#define RESTORE_REGS				 \
	popq %r11						;\
	popq %r10						;\
	popq %r9						;\
	popq %r8						;\
	popq %rsi						;\
	popq %rdi						;\
	popq %rdx						;\
	popq %rcx						;\
	popq %rax

.align 64
.type default_trap,%function
default_trap:
	cli
	SAVE_REGS
	/* TODO */
	call default_trap_c
	RESTORE_REGS
1:	jmp 1b

/* void task_init(void *tcb, void *entry, void *stack) */
.align 64
.type task_init,%function
task_init:
	movq %rdx, 136(%rdi)	/* stack */
	pushfq
	popq 128(%rdi)			/* flags (current flags) */
	movq %rsi, 120(%rdi)	/* instruction pointer -> entry */
	xorq %rdx, %rdx			/* zero out all other registers */
	movq %rdx, 112(%rdi)
	movq %rdx, 104(%rdi)
	movq %rdx, 96(%rdi)
	movq %rdx, 88(%rdi)
	movq %rdx, 80(%rdi)
	movq %rdx, 72(%rdi)
	movq %rdx, 64(%rdi)
	movq %rdx, 56(%rdi)
	movq %rdx, 48(%rdi)
	movq %rdx, 40(%rdi)
	movq %rdx, 32(%rdi)
	movq %rdx, 24(%rdi)
	movq %rdx, 16(%rdi)
	movq %rdx, 8(%rdi)
	movq %rdx, (%rdi)
	ret

.align 64
.type task_start,%function
/* void task_start(void *tcb) */
task_start:
	pushq 128(%rdi)
	popfq
	movq 136(%rdi), %rsp
	pushq 120(%rdi)
	movq %rdi, curr_task	/* initialize curr_task */
	ret

.align 64
.type timer_apic,%function
timer_apic:
	cli
	pushq %rax
	movq curr_task, %rax
	testq %rax, %rax		/* curr_task is NULL */
	jz .uninitialized

	/* Store the current task state. */
	popq (%rax)
	movq %rdx, 8(%rax)
	movq %rbx, 16(%rax)
	movq %rcx, 24(%rax)
	movq %rsi, 32(%rax)
	movq %rdi, 40(%rax)
	movq %r8, 48(%rax)
	movq %r9, 56(%rax)
	movq %r10, 64(%rax)
	movq %r11, 72(%rax)
	movq %r12, 80(%rax)
	movq %r13, 88(%rax)
	movq %r14, 96(%rax)
	movq %r15, 104(%rax)
	movq %rbp, 112(%rax)
	movq (%rsp), %rdx		/* instruction pointer */
	movq %rdx, 120(%rax)
	movq 16(%rsp), %rdx		/* flags */
	movq %rdx, 128(%rax)
	movq 24(%rsp), %rdx		/* stack */
	movq %rdx, 136(%rax)

	/* Call the C handler, it changes curr_task. */
	call timer_apic_handler

	/* Restore the next task state. */
	movq curr_task, %rax
	movq 136(%rax), %rdx
	movq %rdx, 24(%rsp)
	movq 128(%rax), %rdx
	movq %rdx, 16(%rsp)
	movq 120(%rax), %rdx
	movq %rdx, (%rsp)
	movq 112(%rax), %rbp
	movq 104(%rax), %r15
	movq 96(%rax), %r14
	movq 88(%rax), %r13
	movq 80(%rax), %r12
	movq 72(%rax), %r11
	movq 64(%rax), %r10
	movq 56(%rax), %r9
	movq 48(%rax), %r8
	movq 40(%rax), %rdi
	movq 32(%rax), %rsi
	movq 24(%rax), %rcx
	movq 16(%rax), %rbx
	movq 8(%rax), %rdx
	movq (%rax), %rax

	sti
	iretq

.uninitialized:
	SAVE_REGS_NORAX

	/* Call the C handler, EOI only, curr_task is still NULL. */
    call timer_apic_handler

	RESTORE_REGS
	sti
	iretq
