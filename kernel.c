/*
 * kernel.c - main kernel file (Assignment 1, CSE 597)
 * Copyright 2025 Ruslan Nikolaev <rnikola@psu.edu>
 */

#include <types.h>
#include <multiboot2.h>
#include <fb.h>
#include <apic.h>
#include <printf.h>
#include <kernel.h>
#include "iso9660.h"
#define PG_BYTES          4096ULL
#define PT_ENTRIES        512ULL
#define PT_RW_PRESENT     0x3ULL   
unsigned int APIC_TIMER_VECTOR = 0x50;

extern void task_init(void *tcb, void *entry, void *stack_top);
extern void task_start(void *tcb);

void timer_apic_handler(void);

static void find_iso_module(uint32_t mb_addr, uint32_t *mod_start, uint32_t *mod_size)
{
    struct multiboot_tag *tag;

    // multiboot info begins with total_size + reserved (8 bytes)
    tag = (struct multiboot_tag *)(mb_addr + 8);

    while (tag->type != MULTIBOOT_TAG_TYPE_END) {

        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *m = (struct multiboot_tag_module *)tag;

            *mod_start = m->mod_start;
            *mod_size  = m->mod_end - m->mod_start;

            printf("ISO module detected: start=%x size=%u\n",
                   *mod_start, *mod_size);

            return;
        }

        // move to next tag (8-byte aligned)
        tag = (struct multiboot_tag *)(((uintptr_t)tag + tag->size + 7) & ~7ULL);
    }

    printf("No ISO module found.\n");
}


static inline void *page_align(void *base)
{
	return (void *) (((uint64_t) base + 4095) & (~4095ULL));
}

static inline void write_cr3(uint64_t cr3_value)
{
	asm volatile ("mov %0, %%cr3"
	:
	: "r" (cr3_value)
	: "memory");
}

static uint64_t build_identity_4g_tables(void **freemem)
{
	const uint64_t total_pte   = 1024ULL * 1024ULL;  
	const uint64_t num_pt_pg   = 2048ULL;            
	const uint64_t num_pd_pg   = 4ULL;               
	const uint64_t pages_total = num_pt_pg + num_pd_pg + 1 + 1;

	uintptr_t base = (uintptr_t) page_align(*freemem);

	uint64_t *pt_all   = (uint64_t *) base;                                         
	uint64_t *pd_all   = (uint64_t *) (base + num_pt_pg * PG_BYTES);                
	uint64_t *pdp_page = (uint64_t *) (base + (num_pt_pg + num_pd_pg) * PG_BYTES);  
	uint64_t *pml4     = (uint64_t *) (base + (num_pt_pg + num_pd_pg + 1) * PG_BYTES);

	const uint64_t qwords = (pages_total * PG_BYTES) / sizeof(uint64_t);
	for (uint64_t i = 0; i < qwords; ++i)
		((uint64_t *) base)[i] = 0;

	
	for (uint64_t i = 0; i < total_pte; ++i) {
		uint64_t page_byte_address = i * PG_BYTES;      
		pt_all[i] = page_byte_address | PT_RW_PRESENT;
	}
	
	for (uint64_t j = 0; j < num_pt_pg; ++j) {
		uint64_t pt_phys = (uint64_t)(uintptr_t)(pt_all + j * PT_ENTRIES);
		pd_all[j] = pt_phys | PT_RW_PRESENT;
	}

	for (uint64_t k = 0; k < 4; ++k) {
		uint64_t pd_phys = (uint64_t)(uintptr_t)(pd_all + k * PT_ENTRIES);
		pdp_page[k] = pd_phys | PT_RW_PRESENT;
	}

	pml4[0] = ((uint64_t)(uintptr_t)pdp_page) | PT_RW_PRESENT;

	*freemem = (void *)(base + pages_total * PG_BYTES);
	return (uint64_t)(uintptr_t)pml4;
}

struct idt_gate {
    unsigned long off_lo:16;   
    unsigned long sel:16;      
    unsigned long ist:3;       
    unsigned long _r1:5;       
    unsigned long type:5;      
    unsigned long dpl:2;       
    unsigned long p:1;         
    unsigned long off_hi:48;   
    unsigned long _r2:8;       
    unsigned long z:5;         
    unsigned long _r3:19;      
} __attribute__((packed));

static struct idt_gate idt[256] __attribute__((aligned(16)));
static idt_pointer_t idtp;   

extern void default_trap(void);  


static void idt_set_gate(int vec, void *fn, int ist)
{
    unsigned long addr = (unsigned long)fn;
    struct idt_gate *g = &idt[vec];

    g->off_lo = (unsigned short)(addr & 0xFFFF);
    g->off_hi = (unsigned long)(addr >> 16);
    g->sel    = 0x10;          
    g->ist    = ist & 7;
    g->type   = 0xE;
    g->dpl    = 0;
    g->p      = 1;
    g->z      = 0;
    g->_r1 = g->_r2 = g->_r3 = 0;
}

extern void timer_apic(void);     
static volatile uint64_t tick_counter = 0ULL;

typedef struct task_frame {
    uint64_t rax;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rbp;
    uint64_t rip;
    uint64_t rflags;
    uint64_t rsp;
} task_frame_t;

volatile task_frame_t *curr_task = NULL;

static task_frame_t *g_tasks[2] = { NULL, NULL };
static unsigned      g_curr_idx  = 0;

static inline void *alloc_page(void **freep) {
    uintptr_t p = ((uintptr_t)(*freep) + 4095ULL) & ~4095ULL;
    *freep = (void *)(p + 4096ULL);
    return (void *)p;
}

static inline void setup_timer_gate(void)
{
    idt_set_gate(APIC_TIMER_VECTOR, timer_apic, 0);
}

static void idt_init(void)
{
    for(int i = 0; i < 256; ++i) {
        idt_set_gate(i, default_trap, 0);
	}
	setup_timer_gate();

    idtp.limit = (unsigned short)(sizeof(idt) - 1);
    idtp.base  = (unsigned long long)(uintptr_t)idt;
    load_idt(&idtp);
}

void default_trap_c(void)
{
    printf("\nError occurred. Halted.\n");
    for (;;) { __asm__ __volatile__("cli; hlt"); }
}

void apic_timer(void)
{
    tick_counter++;
    if ((tick_counter & 0x1FULL) == 0ULL) {
        printf("Timer!\n");
    }
    x86_lapic_write(X86_LAPIC_EOI, 0);
}

void timer_apic_handler(void)
{
    if (curr_task == NULL) {
        x86_lapic_write(X86_LAPIC_EOI, 0);
        return;
    }

	if (g_curr_idx == 0) g_curr_idx = 1;
	else g_curr_idx = 0;

	curr_task = g_tasks[g_curr_idx];

    x86_lapic_write(X86_LAPIC_EOI, 0);
}



/* Multiboot2 header */
struct multiboot_info {
	uint32_t total_size;
	uint32_t pad;
};

/* Locate the framebuffer address */
void *find_fb(struct multiboot_info *info)
{
	struct multiboot_tag *curr = (struct multiboot_tag *) (info + 1);
	while (curr->type != MULTIBOOT_TAG_TYPE_END) {
		if (curr->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
			struct multiboot_tag_framebuffer *fb =
				(struct multiboot_tag_framebuffer *) curr;
			if (fb->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB
					&& fb->common.framebuffer_bpp == 32
					&& fb->common.framebuffer_width == 800
					&& fb->common.framebuffer_height == 600
					&& fb->common.framebuffer_pitch == 3200) {
				return (void *) fb->common.framebuffer_addr;
			}
		}
		curr = (struct multiboot_tag *) 
			(((uintptr_t) curr + curr->size + 7ULL) & ~7ULL);
	}
	return NULL;
}

void init_apic_timer(void)
{
    x86_lapic_enable();
    x86_lapic_write(X86_LAPIC_TIMER_DIVIDE, 0x0A);
	x86_lapic_write(X86_LAPIC_TIMER, (1u << 17) | (uint32_t)(APIC_TIMER_VECTOR));
	x86_lapic_write(X86_LAPIC_TIMER_INIT, 200000U);
   printf("APIC timer is set up using vector %u.\n", (unsigned)(APIC_TIMER_VECTOR & 0xFFu));

}

static void demo_shell()
{
    printf("\nMiniOS> ls\n");
    iso9660_list_root();

    printf("\nMiniOS> cat HELLO.TXT\n");
    iso9660_read_file("HELLO.TXT");

    printf("\nMiniOS> cat TEST.TXT\n");
    iso9660_read_file("TEST.TXT");
}


void kernel_start(struct multiboot_info *info, void *free_mem_base)
{
	fb_init(find_fb(info), 800, 600);

    uint32_t iso_start = 0;
    uint32_t iso_size  = 0;

    find_iso_module((uint32_t)info, &iso_start, &iso_size);

    iso9660_init(iso_start, iso_size);
    // iso9660_list_root();

    // iso9660_read_file("HELLO.TXT");
    demo_shell();


	idt_init();
    void *freemem = free_mem_base;               
	uint64_t pml4_phys = build_identity_4g_tables(&freemem);
	write_cr3(pml4_phys);

	printf("Paging on. PML4 is at address %llu.\n", (unsigned long long)pml4_phys);


	while (1) {} /* Never return! */
}
