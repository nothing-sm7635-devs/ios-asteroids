/*
 * Nothing Phone (3a) probe 1: one `fastboot boot` answers whether the
 * ios-sweet approach can work under Qualcomm's Gunyah hypervisor.
 *   - entry state (EL, MPIDR, DTB, load address) and the ABL framebuffer
 *   - the Gunyah virtual watchdog: armed? can we pet it?
 *   - CPU features (16K granule, GIC sysreg interface), PSCI/SMCCC
 *   - SMP: PSCI CPU_ON for the 7 secondaries
 *   - 16K stage-1 MMU under Gunyah's stage 2
 *   - (virtual) GICv3: Group 1 timer IRQs and SGIs
 * Each line is drawn as soon as its test finishes, so a hang shows where.
 * Afterwards it stays on screen, petting the watchdog once a second.
 */
#include <stdint.h>
#include "fbcon.h"
#include "mmu.h"

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
#define GICR_IPRIORITYR		(GICR_SGI + 0x400)
#define VTIMER_INTID		27	/* DT: <GIC_PPI 11 ...> */
#define PTIMER_INTID		30	/* DT: <GIC_PPI 14 ...> */
#define SGI_INTID		1

#define PSCI_VERSION		0x84000000UL
#define PSCI_CPU_OFF		0x84000002UL
#define PSCI_CPU_ON		0xc4000003UL
#define PSCI_SYSTEM_OFF		0x84000008UL
#define PSCI_SYSTEM_RESET	0x84000009UL
#define PSCI_FEATURES		0x8400000aUL
#define PSCI_SYSTEM_RESET2	0xc4000012UL
#define SMCCC_VERSION		0x80000000UL

/* Gunyah vendor-hyp SMCCC calls, drivers/virt/gunyah/gh_virt_wdt.c */
#define GH_WDT_CONTROL		0x86000005UL
#define GH_WDT_STATUS		0x86000006UL
#define GH_WDT_PET		0x86000007UL

#define NR_CPUS			8

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

extern char _head[], secondary_entry[];

static const char *stage;	/* set at runtime: no absolute pointers in data */
static volatile uint64_t sec_mpidr[NR_CPUS];
static volatile uint32_t irq_count, irq_other, irq_last;
static volatile int irq_src;
static int status_row;

struct smc_res {
	uint64_t a0, a1, a2, a3;
};

static struct smc_res smc4(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
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

static uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	return smc4(fn, a1, a2, a3).a0;
}

static uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

static void delay_ms(unsigned ms)
{
	uint64_t end = now() + rsr("cntfrq_el0") / 1000 * ms;
	while (now() < end)
		;
}

static uint32_t to_us(uint64_t ticks)
{
	return ticks * 10 / 192;	/* 19.2 MHz */
}

/* Keep the result screen up forever; the watchdog gets petted every second. */
static void __attribute__((noreturn)) hold(void)
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

/* ---- exceptions ---- */

static void irq_handler(void)
{
	uint64_t iar = rsr("icc_iar1_el1");
	uint32_t id = iar & 0xffffff;

	if (id >= 1020 && id <= 1023)
		return;	/* spurious */
	irq_last = id;
	if (id == (uint32_t)irq_src)
		irq_count++;
	else
		irq_other++;
	if (id == VTIMER_INTID)
		wsr("cntv_tval_el0", rsr("cntfrq_el0") / 100);	/* 10 ms */
	else if (id == PTIMER_INTID)
		wsr("cntp_tval_el0", rsr("cntfrq_el0") / 100);
	wsr("icc_eoir1_el1", iar);
	isb();
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	if (slot == 5) {	/* IRQ from current EL, SPx */
		irq_handler();
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

/* ---- SMP ---- */

void secondary_main(uint64_t idx)
{
	sec_mpidr[idx] = rsr("mpidr_el1") | (1ULL << 63);
	__asm__ volatile("dsb sy" ::: "memory");
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

static void test_smp(void)
{
	stage = "smp";
	uint64_t self = rsr("mpidr_el1") & 0xffffff;
	int64_t ret[NR_CPUS];

	for (int i = 1; i < NR_CPUS; i++)
		ret[i] = smc(PSCI_CPU_ON, (uint64_t)i << 8, (uintptr_t)secondary_entry, i);
	delay_ms(500);
	for (int i = 1; i < NR_CPUS; i++) {
		char label[] = "CPU0";
		label[3] = '0' + i;
		if (((uint64_t)i << 8) == self)
			con_str(label, "is boot cpu");
		else
			con_hex2(label, "ret=", (uint32_t)ret[i], "mpidr=",
				 (uint32_t)sec_mpidr[i] | (sec_mpidr[i] ? 0 : 0xdead0000));
	}
}

/* ---- GIC + timer ---- */

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

static void gicd_wait_rwp(void)
{
	for (int i = 0; i < 1000000 && (rd32(GICD_BASE) & (1U << 31)); i++)
		;
}

static uint64_t sgi_self(unsigned id)
{
	uint64_t m = rsr("mpidr_el1");
	return ((m >> 16) & 0xff) << 32 |	/* Aff2 */
	       ((m >> 8) & 0xff) << 16 |	/* Aff1 */
	       (uint64_t)id << 24 |
	       (1U << (m & 0xf));		/* target list: Aff0 */
}

/* Run one interrupt source for ~300 ms and report what arrived. */
static void irq_window(const char *label, uintptr_t r, int src)
{
	uint64_t f = rsr("cntfrq_el0");

	irq_count = irq_other = 0;
	irq_last = 0x3ff;
	irq_src = src;
	if (src == VTIMER_INTID) {
		wsr("cntv_tval_el0", f / 100);
		wsr("cntv_ctl_el0", 1);
	}
	isb();
	if (src == SGI_INTID)
		wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
	isb();
	delay_ms(20);
	uint32_t pend = rd32(r + GICR_ISPENDR0);
	uint32_t hppi = rsr("icc_hppir1_el1");

	__asm__ volatile("msr daifclr, #2" ::: "memory");
	uint64_t t = now() + f * 3 / 10, next = now() + f / 100;
	/* SGI: one already sent above, send 9 more 10 ms apart -> expect 10 */
	for (int sent = 1; now() < t;)
		if (src == SGI_INTID && sent < 10 && now() >= next) {
			wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
			isb();
			sent++;
			next += f / 100;
		}
	__asm__ volatile("msr daifset, #2" ::: "memory");
	wsr("cntv_ctl_el0", 0);
	isb();

	con_hex2(label, "irqs=", irq_count, "other=", irq_other);
	con_hex2(" pre", "pend=", pend, "hppi=", hppi);
	con_hex2(" ", "last=", irq_last, 0, 0);
}

static void test_gic(void)
{
	stage = "gic sre";
	uint64_t sre = rsr("icc_sre_el1");
	if (!(sre & 1)) {
		wsr("icc_sre_el1", sre | 1);
		isb();
	}
	con_hex2("ICC_SRE", "was=", sre, "now=", rsr("icc_sre_el1"));
	if (!(rsr("icc_sre_el1") & 1))
		return;

	stage = "gic dist";
	uint32_t dctlr = rd32(GICD_BASE);
	con_hex2("GICD", "ctlr=", dctlr, "typer=", rd32(GICD_BASE + 4));
	con_hex2(" ", "iidr=", rd32(GICD_BASE + 8), "pidr2=", rd32(GICD_BASE + 0xffe8));
	wr32(GICD_BASE, dctlr | 0x13);
	gicd_wait_rwp();
	con_hex2(" ", "ctlr=", rd32(GICD_BASE), 0, 0);

	stage = "gic redist";
	uintptr_t r = find_gicr();
	if (!r) {
		con_str("GICR", "not found");
		return;
	}
	uint32_t waker = rd32(r + GICR_WAKER);
	if (waker & 2) {
		wr32(r + GICR_WAKER, waker & ~2U);
		for (int i = 0; i < 100000 && (rd32(r + GICR_WAKER) & 4); i++)
			;
	}
	con_hex2("GICR", "base=", r, "waker=", rd32(r + GICR_WAKER));

	stage = "gic config";
	for (int id = 0; id < 32; id++)
		wr8(r + GICR_IPRIORITYR + id, 0xa0);
	wr32(r + GICR_ISENABLER0, (1U << VTIMER_INTID) | (1U << SGI_INTID));
	uint64_t ctlr = rsr("icc_ctlr_el1");
	wsr("icc_ctlr_el1", ctlr & ~2ULL);	/* EOImode 0: EOIR also deactivates */
	wsr("icc_pmr_el1", 0xf0);
	wsr("icc_bpr1_el1", 0);
	wsr("icc_igrpen1_el1", 1);
	isb();
	con_hex2("ICC", "ctlr=", ctlr, "grpen1=", rsr("icc_igrpen1_el1"));

	stage = "gic vtimer";
	irq_window("VTIMER", r, VTIMER_INTID);
	stage = "gic sgi";
	irq_window("SGI1", r, SGI_INTID);
	wr32(r + GICR_ICENABLER0, 0xffffffff);
	isb();
}

/* ---- main ---- */

static uint64_t scratch[8192];	/* 64KB, inside our (later cacheable) image */

static uint64_t ticks_mem(void)
{
	uint64_t t = now();
	for (int pass = 0; pass < 16; pass++)
		for (int i = 0; i < 8192; i++)
			scratch[i] = scratch[i] * 3 + i;
	return now() - t;
}

static uint64_t ticks_fill(void)
{
	uint64_t t = now();
	con_clear(NAVY);
	return now() - t;
}

static void psci_feat(const char *label, uint64_t a, uint64_t b)
{
	con_hex2(label, "a=", smc(PSCI_FEATURES, a, 0, 0), "b=", smc(PSCI_FEATURES, b, 0, 0));
}

void shim_main(uint64_t dtb)
{
	struct mmu_info mi;
	uint64_t mmfr0 = rsr("id_aa64mmfr0_el1");

	stage = "fb";
	fb_detect();
	uint64_t fill_off = ticks_fill();
	con_str("PROBE", "asteroids probe 1");
	status_row = con_row();
	con_skip();	/* UPTIME, WDT and pet lines are filled in by hold() */
	con_skip();
	con_skip();
	con_skip();

	stage = "entry";
	con_hex2("ENTRY", "el=", rsr("currentel") >> 2, "mpidr=", rsr("mpidr_el1"));
	con_hex2(" ", "midr=", rsr("midr_el1"), "dtb=", dtb);
	con_hex2(" ", "magic=", dtb ? __builtin_bswap32(rd32(dtb)) : 0, "head=", (uintptr_t)_head);
	con_hex2("FB", "pipe=", fb.pipe, "base=", fb.base);
	con_hex2(" ", "w=", fb.w, "h=", fb.h);
	con_hex2(" ", "stride=", fb.stride, "bpp=", fb.bpp);
	con_hex2(" ", "fmt=", fb.format, "unpack=", fb.unpack);

	stage = "wdt";
	struct smc_res st = smc4(GH_WDT_STATUS, 0, 0, 0);
	con_hex2("WDT0", "ret=", st.a0, "ctl=", st.a1);
	con_hex2(" ", "since=", st.a2, "pet=", smc(GH_WDT_PET, 0, 0, 0));

	stage = "cpu";
	con_hex2("MMFR0", "tg16=", (mmfr0 >> 20) & 0xf, "pa=", mmfr0 & 0xf);
	con_hex2("PFR0", "gic=", (rsr("id_aa64pfr0_el1") >> 24) & 0xf, "el2=", (rsr("id_aa64pfr0_el1") >> 8) & 0xf);

	stage = "psci";
	con_hex2("PSCI", "ver=", smc(PSCI_VERSION, 0, 0, 0), "smccc=", smc(SMCCC_VERSION, 0, 0, 0));
	psci_feat(" on/off", PSCI_CPU_ON, PSCI_CPU_OFF);
	psci_feat(" rst/2", PSCI_SYSTEM_RESET, PSCI_SYSTEM_RESET2);
	smc(GH_WDT_PET, 0, 0, 0);

	test_smp();
	smc(GH_WDT_PET, 0, 0, 0);

	stage = "mmu";
	uint64_t mem_off = ticks_mem();
	int rc = mmu_enable(&mi, fb.base, fb.base + fb.h * fb.stride);
	con_hex2("MMU", "rc=", rc, "l3=", mi.n_l3);
	if (!rc) {
		uint64_t mem_on = ticks_mem();
		con_hex("SCTLR", mi.sctlr_after);
		con_hex("PAR img", mmu_probe((uintptr_t)shim_main));
		con_hex("PAR fb", mmu_probe(fb.base + 0x1000));
		con_hex2("MEM us", "off=", to_us(mem_off), "on=", to_us(mem_on));
		/* repaint once with the NC mapping to time it; redraws below */
		int r = con_row();
		uint64_t t = now();
		for (uint32_t y = fb.h - 64; y < fb.h; y++)
			for (uint32_t x = 0; x < fb.w * fb.bpp; x += 4)
				*(volatile uint32_t *)(fb.base + y * fb.stride + x) = 0x40404040;
		uint64_t fill64 = now() - t;
		con_set_row(r);
		con_hex2("FILL us", "full=", to_us(fill_off), "64on=", to_us(fill64));
	}
	smc(GH_WDT_PET, 0, 0, 0);

	test_gic();

	stage = "done";
	con_skip();
	con_str("DONE", "photo this, then hold");
	con_str(" ", "power + vol- for fastboot");
	hold();
}
