#pragma once
#include <stdint.h>

#define PSCI_VERSION		0x84000000UL
#define PSCI_CPU_OFF		0x84000002UL
#define PSCI_CPU_ON		0xc4000003UL
#define PSCI_SYSTEM_RESET	0x84000009UL
#define PSCI_FEATURES		0x8400000aUL
#define PSCI_SYSTEM_RESET2	0xc4000012UL
#define SMCCC_VERSION		0x80000000UL

/* Gunyah vendor-hyp SMCCC calls, drivers/virt/gunyah/gh_virt_wdt.c */
#define GH_WDT_CONTROL		0x86000005UL
#define GH_WDT_STATUS		0x86000006UL
#define GH_WDT_PET		0x86000007UL

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")

/*
 * MMIO as single plain ldr/str: Gunyah emulates trapped devices (the apps
 * SMMU at least) only from a valid ESR syndrome, which writeback/pair forms
 * do not give - probe 8 run 4 took an external abort on "ldr w, [x], #4".
 */
static inline uint32_t rd32(uintptr_t a)
{
	uint32_t v;
	__asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(a) : "memory");
	return v;
}
static inline uint64_t rd64(uintptr_t a)
{
	uint64_t v;
	__asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"(a) : "memory");
	return v;
}
static inline void wr32(uintptr_t a, uint32_t v)
{
	__asm__ volatile("str %w0, [%1]" : : "r"(v), "r"(a) : "memory");
}
static inline void wr8(uintptr_t a, uint8_t v)
{
	__asm__ volatile("strb %w0, [%1]" : : "r"(v), "r"(a) : "memory");
}

struct smc_res {
	uint64_t a0, a1, a2, a3;
};

extern const char *stage;	/* set at runtime: no absolute pointers in data */
extern int status_row;		/* where hold() draws UPTIME / WDT */

struct smc_res smc4(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3);
uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3);
uint64_t now(void);
void delay_ms(unsigned ms);
uint32_t to_us(uint64_t ticks);
void __attribute__((noreturn)) hold(void);

/* Each probe provides its IRQ handler; exc_handler() calls it for slot 5. */
void probe_irq(void);
