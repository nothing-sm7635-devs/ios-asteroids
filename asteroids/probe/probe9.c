/*
 * Nothing Phone (3a) probe 9: how fast do the cores actually run after ABL?
 * Counts PMU cycles (PMCCNTR_EL0) against the 19.2 MHz generic timer for
 * 50 ms on CPU 0 (Cortex-A520) and on CPU 4 (Cortex-A720, started with
 * PSCI CPU_ON), three times each, and prints MHz. Nothing is written
 * except the screen and the PMU control of the measuring core.
 * If the hypervisor traps PMU access, the exception screen shows it.
 */
#include <stdint.h>
#include "common.h"
#include "fbcon.h"

extern char secondary_entry[];

static volatile uint32_t sec_mhz[3], sec_done, sec_midr;

static uint32_t measure_mhz(void)
{
	uint64_t f = rsr("cntfrq_el0");
	wsr("pmcr_el0", rsr("pmcr_el0") | 1 | 4);	/* E, reset cycle counter */
	wsr("pmcntenset_el0", 1UL << 31);		/* enable PMCCNTR */
	isb();
	uint64_t t0 = now(), c0 = rsr("pmccntr_el0");
	while (now() - t0 < f / 20)
		;
	uint64_t t1 = now(), c1 = rsr("pmccntr_el0");
	return (uint32_t)((c1 - c0) * f / (t1 - t0) / 1000000);
}

void probe_irq(void)
{
}

void secondary_main(uint64_t idx)
{
	(void)idx;
	sec_midr = (uint32_t)rsr("midr_el1");
	for (int i = 0; i < 3; i++)
		sec_mhz[i] = measure_mhz();
	__asm__ volatile("dsb sy" ::: "memory");
	sec_done = 1;
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

void shim_main(uint64_t dtb)
{
	(void)dtb;
	stage = "fb";
	fb_detect();
	con_clear(NAVY);
	con_str("PROBE", "asteroids probe 9 (CPU clock)");
	status_row = con_row();
	con_skip();
	con_skip();
	con_skip();
	con_skip();

	stage = "pmu cpu0";
	con_hex2("CPU0", "midr=", (uint32_t)rsr("midr_el1"), "pmcr=", (uint32_t)rsr("pmcr_el0"));
	for (int i = 0; i < 3; i++)
		con_dec(" MHz", measure_mhz());

	stage = "pmu cpu4";
	uint64_t ret = smc(PSCI_CPU_ON, 4 << 8, (uintptr_t)secondary_entry, 4);
	for (int i = 0; i < 2000 && !sec_done; i++)
		delay_ms(1);
	con_hex2("CPU4", "on=", (uint32_t)ret, "midr=", sec_midr);
	if (sec_done)
		for (int i = 0; i < 3; i++)
			con_dec(" MHz", sec_mhz[i]);
	else
		con_str(" ", "no result from CPU 4");

	stage = "done";
	con_skip();
	con_str("DONE", "photo this, then hold");
	con_str(" ", "power + vol- for fastboot");
	hold();
}
