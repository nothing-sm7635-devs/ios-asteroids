// SPDX-License-Identifier: GPL-2.0-only
/*
 * Derived from GPL-2.0 sources in the Nothing Phone (3a) kernel tree
 * (sm7635, see asteroids/CREDITS.md): SPMI PMIC arbiter v7 APID map, owner
 * table, observer-channel layout and read command (drivers/spmi/
 * spmi-pmic-arb.c), PMIC GPIO registers (drivers/pinctrl/qcom/
 * pinctrl-spmi-gpio.c); structure follows xnu sweet_keys.c (sweet, v5).
 * Copyright (c) 2012-2021, The Linux Foundation.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2021-2024, Qualcomm Innovation Center, Inc.
 */
/*
 * Nothing Phone (3a) probe 7 (read-only): can we read the buttons?
 *   power        PMK8550 SID 0 PON_HLOS 0x1300, RT_STS 0x1310 bit 7 (KPDPWR)
 *   volume down  same register, bit 6 (RESIN)
 *   volume up    PMXR2230 SID 1 GPIO 6 = peripheral 0x8D00, RT_STS bit 0,
 *                active low (live DT gpio-keys vol_up)
 *   AI key       TLMM GPIO 71, active low (gpio-keys ai_key)
 * SPMI reads go through the apps (EE 0) observer channel; the only MMIO
 * writes issue EXT_READL commands. Then it loops, showing the live state and
 * press counts: press each key a few times.
 */
#include <stdint.h>
#include "common.h"
#include "fbcon.h"

#define ARB_CNFG	0x0c42d000UL	/* live DT reg-names: cnfg core chnls obsrvr intr */
#define ARB_CORE	0x0c400000UL
#define ARB_CORE_SIZE	0x3000U
#define ARB_OBS		0x0c440000UL
#define ARB_FEATURES	0x0004
#define APID_MAP(n)	(0x2000 + 4 * (n))	/* pmic_arb_apid_map_offset_v7 */
#define OBS_CH(apid)	(0x20 * (apid))		/* 0x8000 * ee(0) + 0x20 * apid */
#define OBS_CMD		0x00
#define OBS_STATUS	0x08
#define OBS_RDATA0	0x18
#define ST_DONE		1U
#define ST_ERR		0xeU			/* failure | denied | dropped */
#define NO_APID		0xffffU

#define PPID_PON	0x013		/* SID 0, peripheral 0x13 */
#define PPID_VOLUP	0x18d		/* SID 1, peripheral 0x8d (GPIO 6) */

#define TLMM_IO(n)	(0x0f100000UL + (n) * 0x1000UL + 4)
#define PIN_AI_KEY	71

static uint32_t last_status;

static uint16_t find_apid(uint16_t ppid, uint32_t count, uint32_t *owner)
{
	uint16_t found = NO_APID;
	for (uint32_t i = 0; i < count && APID_MAP(i) < ARB_CORE_SIZE; i++) {
		uint32_t map = rd32(ARB_CORE + APID_MAP(i));
		if (!map || ((map >> 8) & 0xfff) != ppid)
			continue;
		uint32_t own = rd32(ARB_CNFG + 4 * i) & 7;
		if (found == NO_APID || own == 0) {	/* prefer apps (EE 0) */
			found = (uint16_t)i;
			*owner = own;
		}
	}
	return found;
}

/* EXT_READL of one byte: (1 << 27) | (addr & 0xff) << 4 | (len - 1) */
static int spmi_read(uint16_t apid, uint16_t addr, uint8_t *out)
{
	if (apid == NO_APID)
		return -1;
	uintptr_t ch = ARB_OBS + OBS_CH(apid);
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(ch + OBS_CMD, (1U << 27) | (uint32_t)(addr & 0xff) << 4);
	__asm__ volatile("dsb sy" ::: "memory");
	uint64_t end = now() + rsr("cntfrq_el0") / 1000;	/* 1 ms */
	while (now() < end) {
		last_status = rd32(ch + OBS_STATUS);
		if (last_status & ST_DONE) {
			if (last_status & ST_ERR)
				return -1;
			*out = (uint8_t)rd32(ch + OBS_RDATA0);
			return 0;
		}
	}
	return -1;
}

void probe_irq(void)
{
}

void secondary_main(uint64_t idx)
{
	(void)idx;
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

void shim_main(uint64_t dtb)
{
	(void)dtb;
	stage = "fb";
	fb_detect();
	con_clear(NAVY);
	con_str("PROBE", "asteroids probe 7 (keys, RO)");
	status_row = con_row();
	con_skip();
	con_skip();
	con_skip();
	con_skip();

	stage = "arb";
	uint32_t ver = rd32(ARB_CORE), count = rd32(ARB_CORE + ARB_FEATURES) & 0x7ff;
	con_hex2("ARB", "ver=", ver, "apids=", count);
	if (ver < 0x70000000U || !count || count > 1024) {
		con_str("ARB", "not a v7 arbiter: stop");
		goto out;
	}
	uint32_t own_pon = 0xff, own_vu = 0xff;
	uint16_t apid_pon = find_apid(PPID_PON, count, &own_pon);
	uint16_t apid_vu = find_apid(PPID_VOLUP, count, &own_vu);
	con_hex2(" pon", "apid=", apid_pon, "owner=", own_pon);
	con_hex2(" volup", "apid=", apid_vu, "owner=", own_vu);

	stage = "regs";
	uint8_t v = 0;
	int r = spmi_read(apid_pon, 0x1310, &v);
	con_hex2("PON", "rt=", r ? 0xbad00000 | last_status : v, "st=", last_status);
	static const uint16_t gregs[4] = { 0x8d05, 0x8d40, 0x8d46, 0x8d10 };	/* subtype mode en rt */
	uint32_t g[4];
	for (int i = 0; i < 4; i++)
		g[i] = spmi_read(apid_vu, gregs[i], &v) ? 0xbad00000 | last_status : v;
	con_hex2("GPIO6", "sub=", g[0], "mode=", g[1]);
	con_hex2(" ", "en=", g[2], "rt=", g[3]);
	con_hex2("AIKEY", "io=", rd32(TLMM_IO(PIN_AI_KEY)), 0, 0);
	smc(GH_WDT_PET, 0, 0, 0);

	stage = "loop";
	con_skip();
	con_str("KEYS", "press power, vol+, vol-, AI key");
	int row = con_row();
	uint32_t cnt[4] = { 0, 0, 0, 0 }, prev[4] = { 0, 0, 0, 0 }, errs = 0;
	uint64_t f = rsr("cntfrq_el0"), t0 = now(), next_pet = now();
	for (;;) {
		uint8_t pon = 0, gp = 0;
		int ep = spmi_read(apid_pon, 0x1310, &pon);
		int eg = spmi_read(apid_vu, 0x8d10, &gp);
		uint32_t st[4] = {
			!ep && (pon & 0x80),		/* power */
			!eg && !(gp & 1),		/* vol+ (active low) */
			!ep && (pon & 0x40),		/* vol- */
			!(rd32(TLMM_IO(PIN_AI_KEY)) & 1),	/* AI key (active low) */
		};
		errs += (ep != 0) + (eg != 0);
		for (int k = 0; k < 4; k++) {
			if (st[k] && !prev[k])
				cnt[k]++;
			prev[k] = st[k];
		}
		con_set_row(row);
		con_hex2("NOW", "pwr/vu=", st[0] << 16 | st[1], "vd/ai=", st[2] << 16 | st[3]);
		con_hex2("PRESSES", "pwr/vu=", cnt[0] << 16 | cnt[1], "vd/ai=", cnt[2] << 16 | cnt[3]);
		con_hex2("RAW", "pon=", pon, "gpio=", gp);
		con_hex2(" ", "errors=", errs, "s=", (uint32_t)((now() - t0) / f));
		if (now() >= next_pet) {
			smc(GH_WDT_PET, 0, 0, 0);
			next_pet = now() + f;
		}
		delay_ms(30);
	}

out:
	stage = "done";
	con_skip();
	con_str("DONE", "photo this, then hold");
	con_str(" ", "power + vol- for fastboot");
	hold();
}
