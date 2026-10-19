/*
 * Helpers shared by the probes: SMCCC calls, timing, the exception screen
 * and the final hold loop that keeps the results up and pets the Gunyah
 * virtual watchdog once a second.
 */
#include "common.h"
#include "fbcon.h"

const char *stage;
int status_row;

struct smc_res smc4(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return (struct smc_res){ x0, x1, x2, x3 };
}

uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	return smc4(fn, a1, a2, a3).a0;
}

uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

void delay_ms(unsigned ms)
{
	uint64_t end = now() + rsr("cntfrq_el0") / 1000 * ms;
	while (now() < end)
		;
}

uint32_t to_us(uint64_t ticks)
{
	return ticks * 10 / 192;	/* 19.2 MHz */
}

void hold(void)
{
	uint64_t f = rsr("cntfrq_el0"), t0 = now();
	for (;;) {
		struct smc_res st = smc4(GH_WDT_STATUS, 0, 0, 0);
		uint64_t pet = smc(GH_WDT_PET, 0, 0, 0);
		int r = con_row();
		con_set_row(status_row);
		con_dec("UPTIME s", (now() - t0) / f);
		con_hex2("WDT", "ctl=", st.a1, "since=", st.a2);
		con_hex2(" pet", "ret=", pet, "st=", st.a0);
		con_set_row(r);
		delay_ms(1000);
	}
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	if (slot == 5) {	/* IRQ from current EL, SPx */
		probe_irq();
		return;
	}
	con_clear(BLUE);
	switch (slot & 3) {
	case 0: con_str("PANIC", "SYNC"); break;
	case 1: con_str("PANIC", "IRQ"); break;
	case 2: con_str("PANIC", "FIQ"); break;
	default: con_str("PANIC", "SERROR"); break;
	}
	con_hex2("SLOT", "n=", slot, "cpu=", rsr("mpidr_el1"));
	con_str("STAGE", stage);
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	con_hex("SPSR", rsr("spsr_el1"));
	con_hex("X0", regs[0]);
	con_hex("X1", regs[1]);
	con_hex("X30", regs[29]);	/* stashed lr is just above the frame */
	con_skip();
	status_row = con_row();
	hold();
}
