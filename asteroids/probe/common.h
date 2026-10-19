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

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

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
