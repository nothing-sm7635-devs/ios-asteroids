/*
 * Nothing Phone (3a) probe 2. Probe 1 got only 1 of 10 self-SGIs (the first,
 * sent with IRQs masked); this one narrows that down and then tries the UFS.
 * Everything runs with the MMU off, so all CPUs see memory coherently.
 *   SGI A: 10 self-SGIs, each sent masked: did it become pending (generation)
 *          and was it taken once unmasked (delivery)?
 *   SGI B: 10 self-SGIs sent unmasked, 10 ms apart (probe 1's case)
 *   SGI C: each SGI ID 0..15 once: which IDs arrive at all
 *   SGI D: CPU0 -> CPU1 and CPU1 -> CPU0, 10 each (what XNU's IPIs need)
 *   UFS:   read-only: host state ABL left, LUN capacities, LUN 1 GPT
 */
#include <stdint.h>
#include "common.h"
#include "fbcon.h"
#include "ufs.h"

#define GICD_BASE		0x17100000UL
#define GICR_BASE		0x17180000UL
#define GICR_SIZE		0x200000UL
#define GICR_PE_SIZE		0x40000UL	/* DT redistributor-stride */
#define GICR_TYPER		0x08
#define GICR_WAKER		0x14
#define GICR_SGI		0x10000
#define GICR_ISENABLER0		(GICR_SGI + 0x100)
#define GICR_ICENABLER0		(GICR_SGI + 0x180)
#define GICR_ISPENDR0		(GICR_SGI + 0x200)
#define GICR_ICPENDR0		(GICR_SGI + 0x280)
#define GICR_ISACTIVER0		(GICR_SGI + 0x300)
#define GICR_IPRIORITYR		(GICR_SGI + 0x400)

#define NR_CPUS			8

extern char secondary_entry[];

/* per-CPU IRQ bookkeeping, indexed by MPIDR Aff1 */
static volatile uint32_t sgi_count[NR_CPUS][16];
static volatile uint32_t irq_other[NR_CPUS], irq_last[NR_CPUS];

static volatile uint32_t sec_state;	/* 1 = CPU1 GIC ready, 2 = CPU0 asks CPU1 to send, 3 = sent */
static volatile uint32_t sec_waker, sec_ret;
static volatile uint32_t sec_stop;

static unsigned cpu_index(void)
{
	return (rsr("mpidr_el1") >> 8) & 0xff;
}

void probe_irq(void)
{
	unsigned cpu = cpu_index() & (NR_CPUS - 1);
	uint64_t iar = rsr("icc_iar1_el1");
	uint32_t id = iar & 0xffffff;

	if (id >= 1020 && id <= 1023)
		return;	/* spurious */
	irq_last[cpu] = id;
	if (id < 16)
		sgi_count[cpu][id]++;
	else
		irq_other[cpu]++;
	wsr("icc_eoir1_el1", iar);
	isb();
}

static void irq_on(void) { __asm__ volatile("msr daifclr, #2; isb" ::: "memory"); }
static void irq_off(void) { __asm__ volatile("msr daifset, #2; isb" ::: "memory"); }

/* ICC_SGI1R_EL1 value for SGI `id` to the CPU with MPIDR Aff1 = cpu (Aff0 = 0). */
static uint64_t sgi_to(unsigned cpu, unsigned id)
{
	return (uint64_t)cpu << 16 | (uint64_t)id << 24 | 1;
}

static void send_sgi(unsigned cpu, unsigned id)
{
	wsr("icc_sgi1r_el1", sgi_to(cpu, id));
	isb();
}

static uintptr_t find_gicr(void)
{
	uint64_t aff = rsr("mpidr_el1");
	uint32_t want = (aff & 0xffffff) | ((aff >> 8) & 0xff000000);

	for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {
		uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == want)
			return GICR_BASE + off;
		if (typer & (1 << 4))	/* Last */
			break;
	}
	return 0;
}

/* Wake this CPU's redistributor, enable SGIs 0-15 at priority 0xa0 (word writes). */
static uintptr_t gic_cpu_init(void)
{
	uintptr_t r = find_gicr();
	if (!r)
		return 0;
	uint32_t waker = rd32(r + GICR_WAKER);
	if (waker & 2) {
		wr32(r + GICR_WAKER, waker & ~2U);
		for (int i = 0; i < 100000 && (rd32(r + GICR_WAKER) & 4); i++)
			;
	}
	for (int w = 0; w < 8; w++)
		wr32(r + GICR_IPRIORITYR + 4 * w, 0xa0a0a0a0);
	wr32(r + GICR_ICPENDR0, 0xffff);
	wr32(r + GICR_ISENABLER0, 0xffff);
	if (!(rsr("icc_sre_el1") & 1)) {
		wsr("icc_sre_el1", rsr("icc_sre_el1") | 1);
		isb();
	}
	wsr("icc_ctlr_el1", rsr("icc_ctlr_el1") & ~2ULL);	/* EOImode 0 */
	wsr("icc_pmr_el1", 0xf0);
	wsr("icc_bpr1_el1", 0);
	wsr("icc_igrpen1_el1", 1);
	isb();
	return r;
}

/* ---- CPU1 side of test D ---- */

void secondary_main(uint64_t idx)
{
	(void)idx;
	uintptr_t r = gic_cpu_init();
	sec_waker = r ? rd32(r + GICR_WAKER) : 0xdead;
	__asm__ volatile("dsb sy" ::: "memory");
	sec_state = 1;
	irq_on();
	while (sec_state != 2 && !sec_stop)
		;
	if (sec_state == 2) {
		for (int i = 0; i < 10; i++) {
			send_sgi(0, 3);
			delay_ms(10);
		}
		sec_state = 3;
	}
	while (!sec_stop)
		;
	irq_off();
	__asm__ volatile("dsb sy" ::: "memory");
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

/* "0123456789" style string of per-trial results */
static void put_digit(char *s, int i, uint32_t v)
{
	s[i] = v < 10 ? '0' + v : (v < 36 ? 'A' + v - 10 : '+');
}

static void test_sgi(void)
{
	char s[16];
	uintptr_t r;

	stage = "gic init";
	con_hex2("GICD", "ctlr=", rd32(GICD_BASE), "iidr=", rd32(GICD_BASE + 8));
	wr32(GICD_BASE, rd32(GICD_BASE) | 0x13);
	for (int i = 0; i < 1000000 && (rd32(GICD_BASE) & (1U << 31)); i++)
		;
	r = gic_cpu_init();
	if (!r) {
		con_str("GICR", "not found");
		return;
	}
	con_hex2("GICR", "base=", r, "waker=", rd32(r + GICR_WAKER));
	con_hex2(" prio", "sgi0-3=", rd32(r + GICR_IPRIORITYR), "en=", rd32(r + GICR_ISENABLER0));
	con_hex2("ICC", "ctlr=", rsr("icc_ctlr_el1"), "pmr=", rsr("icc_pmr_el1"));

	/* A: send masked, look, unmask, look */
	stage = "sgi A";
	for (int i = 0; i < 10; i++) {
		uint32_t before = sgi_count[0][1];
		send_sgi(0, 1);
		delay_ms(1);
		uint32_t pend = rd32(r + GICR_ISPENDR0) >> 1 & 1;
		uint32_t hppi = rsr("icc_hppir1_el1") & 0xffffff;
		irq_on();
		delay_ms(2);
		irq_off();
		/* 0-3: bit0 = pending in GICR, bit1 = HPPI is SGI1; then taken once */
		s[i] = '0' + pend + 2 * (hppi == 1);
		if (sgi_count[0][1] - before != 1)
			s[i] += 'a' - '0';	/* a-d: same, but not taken exactly once */
	}
	s[10] = 0;
	con_str("A pend", s);	/* ideal 3333333333 */
	con_hex2(" ", "total=", sgi_count[0][1], "act=", rd32(r + GICR_ISACTIVER0));
	con_hex2(" ", "rpr=", rsr("icc_rpr_el1"), "pend=", rd32(r + GICR_ISPENDR0));

	/* B: probe 1's case, unmasked, 10 ms apart; digit = count after each send */
	stage = "sgi B";
	uint32_t base = sgi_count[0][1];
	irq_on();
	for (int i = 0; i < 10; i++) {
		send_sgi(0, 1);
		delay_ms(10);
		put_digit(s, i, sgi_count[0][1] - base);
	}
	irq_off();
	s[10] = 0;
	con_str("B count", s);	/* ideal 123456789A */
	con_hex2(" ", "act=", rd32(r + GICR_ISACTIVER0), "pend=", rd32(r + GICR_ISPENDR0));

	/* C: every SGI ID once */
	stage = "sgi C";
	uint32_t got = 0, cnt_before[16];
	for (int id = 0; id < 16; id++)
		cnt_before[id] = sgi_count[0][id];
	irq_on();
	for (int id = 0; id < 16; id++) {
		send_sgi(0, id);
		delay_ms(5);
	}
	irq_off();
	for (int id = 0; id < 16; id++)
		if (sgi_count[0][id] != cnt_before[id])
			got |= 1u << id;
	con_hex2("C ids", "got=", got, "other=", irq_other[0]);	/* ideal ffff */

	/* D: cross-CPU */
	stage = "sgi D";
	sec_ret = smc(PSCI_CPU_ON, 1 << 8, (uintptr_t)secondary_entry, 1);
	for (int i = 0; i < 500 && sec_state != 1; i++)
		delay_ms(1);
	con_hex2("D cpu1", "on=", sec_ret, "waker=", sec_waker);
	if (sec_state != 1) {
		con_str(" ", "cpu1 did not get ready");
	} else {
		for (int i = 0; i < 10; i++) {
			send_sgi(1, 2);
			delay_ms(10);
		}
		delay_ms(10);
		uint32_t to1 = sgi_count[1][2];
		irq_on();
		sec_state = 2;
		for (int i = 0; i < 300 && sec_state != 3; i++)
			delay_ms(1);
		delay_ms(10);
		irq_off();
		con_hex2(" 0->1", "got=", to1, "last1=", irq_last[1]);	/* ideal a */
		con_hex2(" 1->0", "got=", sgi_count[0][3], "st=", sec_state);	/* ideal a, 3 */
	}
	sec_stop = 1;
	wr32(r + GICR_ICENABLER0, 0xffffffff);
}

void shim_main(uint64_t dtb)
{
	(void)dtb;
	stage = "fb";
	fb_detect();
	con_clear(NAVY);
	con_str("PROBE", "asteroids probe 2");
	status_row = con_row();
	con_skip();	/* UPTIME, WDT and pet lines are filled in by hold() */
	con_skip();
	con_skip();
	con_skip();

	test_sgi();
	smc(GH_WDT_PET, 0, 0, 0);
	con_skip();

	stage = "ufs";
	test_ufs();

	stage = "done";
	con_skip();
	con_str("DONE", "photo this, then hold");
	con_str(" ", "power + vol- for fastboot");
	hold();
}
