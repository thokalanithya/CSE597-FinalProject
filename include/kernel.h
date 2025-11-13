#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern char kernel_stack[];

/*
 * NOTE: When declaring the IDT table, make
 * sure it is properly aligned, e.g.,
 *
 * ... idt[256] __attribute__((aligned(16)));
 * rather than just idt[256]
 */

/* Load IDT */

/* Please define and initialize this variable and then load
   this 80-bit "pointer" (it is similar to GDT's 80-bit "pointer") */
struct idt_pointer {
	uint16_t limit;
	uint64_t base;
} __attribute__((__packed__));

typedef struct idt_pointer idt_pointer_t;

static inline void load_idt(idt_pointer_t *idtp)
{
	__asm__ __volatile__ ("lidt %0; sti" /* also enable interrupts */
		:
		: "m" (*idtp)
	);
}

#ifdef __cplusplus
}
#endif
