/*
 * Nothing Phone (3a) probe 4 (read-only): what state does ABL leave the
 * touch path in? FocalTech on QUP0 SE0 SPI (0xa80000), pins GPIO 0-3
 * (qup0_se0_l0..3, function 1), reset GPIO 20, IRQ GPIO 19, rails RPMh
 * ldob22 (vci 3.3 V) and ldob11 (vddio 1.8 V).
 *   GCC     QUP wrapper 0 / SE0 votes, branch CBCRs, SE0 RCG (DFS levels)
 *   TLMM    GPIO 0-3, 19, 20 config and in/out
 *   RPMh    apps RSC DRV2 version (2.x or 3.x register layout), cmd-db,
 *           addresses of ldob22 / ldob11
 *   GENI    SE0 firmware protocol, FIFO geometry, clock select, SPI regs:
 *           only read if GCC shows the wrapper AHB and SE clocks running
 *           (an unclocked QUP access can hang the bus)
 * Nothing is written except the screen (and the watchdog pet).
 */
#include <stdint.h>
#include "common.h"
#include "fbcon.h"

#define GCC_BASE	0x00100000UL
#define GCC_VOTE	0x52008	/* APCS clock branch enable vote: QUP wrap0 */
#define GCC_S0_CBCR	0x18004
#define GCC_M_AHB_CBCR	0x23000
#define GCC_S_AHB_CBCR	0x23004
#define GCC_CORE_CBCR	0x23008
#define GCC_CORE2X_CBCR	0x23018
#define GCC_S0_RCG	0x18010	/* CMD; CFG +4; CMD_DFSR +0x14; PERF_DFSR(l) +0x1c+4l */
#define CBCR_OFF	(1U << 31)

#define TLMM_BASE	0x0f000000UL
#define TLMM_CFG(n)	((n) * 0x1000UL)
#define TLMM_IO(n)	((n) * 0x1000UL + 4)

#define RSC_DRV2	0x17a20000UL
#define CMD_DB		0x81c60000UL	/* reserved-memory aop_cmd_db_region */
#define CMD_DB_SIZE	0x20000U
#define DB_MAGIC	0x0c0330dbU
#define DB_HEADER	144U
#define DB_ENTRY	24U

#define QUP0_BASE	0x00ac0000UL
#define SE0_BASE	0x00a80000UL

static uint32_t gcc(uint32_t off) { return rd32(GCC_BASE + off); }

static uint16_t db16(uint32_t off)
{
	return (uint16_t)(rd32(CMD_DB + (off & ~3U)) >> (8 * (off & 3U)));
}

/* cmd-db: resource name -> RPMh address (read-only port of sweet_rpmh.c rh_lookup) */
static uint32_t db_lookup(const char *res, uint32_t *nmatch)
{
	uint32_t key[2] = { 0, 0 }, found = 0;
	*nmatch = 0;
	for (unsigned i = 0; i < 8 && res[i]; i++)
		key[i / 4] |= (uint32_t)(uint8_t)res[i] << (8 * (i % 4));
	for (unsigned i = 0; i < 8; i++) {
		uint32_t h = 8 + 16 * i;
		unsigned slave = db16(h);
		if (!slave)
			break;
		uint32_t entries = DB_HEADER + db16(h + 2);
		unsigned count = db16(h + 6);
		if (entries + count * DB_ENTRY > CMD_DB_SIZE)
			return 0xbad0;
		for (unsigned j = 0; j < count; j++) {
			uint32_t e = CMD_DB + entries + DB_ENTRY * j;
			if (rd32(e) == key[0] && rd32(e + 4) == key[1]) {
				found = rd32(e + 16);
				(*nmatch)++;
			}
		}
	}
	return found;
}

static void gpio_line(unsigned n)
{
	char label[] = "GPIO  ";
	label[4] = '0' + n / 10;
	label[5] = '0' + n % 10;
	con_hex2(label, "cfg=", rd32(TLMM_BASE + TLMM_CFG(n)), "io=", rd32(TLMM_BASE + TLMM_IO(n)));
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
	con_str("PROBE", "asteroids probe 4 (touch, RO)");
	status_row = con_row();
	con_skip();
	con_skip();
	con_skip();
	con_skip();

	stage = "gcc";
	uint32_t s0 = gcc(GCC_S0_CBCR), mahb = gcc(GCC_M_AHB_CBCR), sahb = gcc(GCC_S_AHB_CBCR);
	con_hex2("GCC", "vote=", gcc(GCC_VOTE), "s0=", s0);
	con_hex2(" ahb", "m=", mahb, "s=", sahb);
	con_hex2(" core", "1x=", gcc(GCC_CORE_CBCR), "2x=", gcc(GCC_CORE2X_CBCR));
	con_hex2(" rcg", "cmd=", gcc(GCC_S0_RCG), "cfg=", gcc(GCC_S0_RCG + 4));
	con_hex2(" dfs", "cmd=", gcc(GCC_S0_RCG + 0x14), "l0=", gcc(GCC_S0_RCG + 0x1c));
	for (unsigned l = 1; l < 8; l += 2)
		con_hex2(" lvl", "a=", gcc(GCC_S0_RCG + 0x1c + 4 * l), "b=",
			 l + 1 < 8 ? gcc(GCC_S0_RCG + 0x1c + 4 * (l + 1)) : 0);

	stage = "tlmm";
	for (unsigned n = 0; n < 4; n++)
		gpio_line(n);
	gpio_line(19);
	gpio_line(20);

	stage = "rsc";
	uint32_t id = rd32(RSC_DRV2);
	con_hex2("RSC", "id=", id, "solver=", rd32(RSC_DRV2 + 4));
	con_hex2(" cfg", "prnt=", rd32(RSC_DRV2 + 0xc), "major=", (id >> 16) & 0xff);

	stage = "cmd-db";
	con_hex2("CMDDB", "ver=", rd32(CMD_DB), "magic=", rd32(CMD_DB + 4));
	if (rd32(CMD_DB + 4) == DB_MAGIC) {
		uint32_t n22, n11;
		uint32_t a22 = db_lookup("ldob22", &n22), a11 = db_lookup("ldob11", &n11);
		con_hex2(" ldob22", "addr=", a22, "n=", n22);
		con_hex2(" ldob11", "addr=", a11, "n=", n11);
	}
	smc(GH_WDT_PET, 0, 0, 0);

	stage = "geni";
	if ((mahb | sahb | s0) & CBCR_OFF) {
		con_str("GENI", "clocks off: SE not read");
	} else {
		con_hex2("QUP0", "hwver=", rd32(QUP0_BASE + 4), 0, 0);
		uint32_t fw = rd32(SE0_BASE + 0x68);
		con_hex2("SE0", "fw=", fw, "proto=", (fw >> 8) & 0xff);	/* 1 = SPI */
		con_hex2(" st", "geni=", rd32(SE0_BASE + 0x40), "ifdis=", rd32(SE0_BASE + 0x64));
		con_hex2(" clk", "sel=", rd32(SE0_BASE + 0x7c), "mcfg=", rd32(SE0_BASE + 0x48));
		con_hex2(" hw", "p0=", rd32(SE0_BASE + 0xe24), "p1=", rd32(SE0_BASE + 0xe28));
		con_hex2(" mode", "dma=", rd32(SE0_BASE + 0x258), "mirq=", rd32(SE0_BASE + 0x610));
		con_hex2(" spi", "cpha=", rd32(SE0_BASE + 0x224), "cpol=", rd32(SE0_BASE + 0x230));
		con_hex2(" spi", "demux=", rd32(SE0_BASE + 0x250), "trans=", rd32(SE0_BASE + 0x25c));
		con_hex2(" spi", "wlen=", rd32(SE0_BASE + 0x268), "ios=", rd32(SE0_BASE + 0x908));
	}

	stage = "done";
	con_skip();
	con_str("DONE", "photo this, then hold");
	con_str(" ", "power + vol- for fastboot");
	hold();
}
