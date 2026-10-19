// SPDX-License-Identifier: GPL-2.0-only
/*
 * Derived from GPL-2.0 sources in the Nothing Phone (3a) kernel tree
 * (sm7635 + sm7635-modules, see asteroids/CREDITS.md):
 *   RPMh RSC v3 TCS offsets and AMC request sequence  drivers/soc/qcom/rpmh-rsc.c
 *   cmd-db lookup                                      drivers/soc/qcom/cmd-db.c
 *   GENI init, FIFO mode and packing                   drivers/soc/qcom/qcom-geni-se.c,
 *                                                      include/linux/soc/qcom/geni-se.h
 *   GENI SPI setup and commands                        drivers/spi/spi-geni-qcom.c
 *   GCC QUP0 clocks, TLMM pins                         drivers/clk/qcom/gcc-volcano.c,
 *                                                      drivers/pinctrl/qcom/pinctrl-volcano.c
 *   FocalTech SPI v2 framing, CRC (crc_kermit is a     touchscreen/focaltech_spi_v2.c,
 *   port of crckermit), power-on/reset order          focaltech_core.c
 * Copyright (c) 2013-2021, The Linux Foundation.
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc.
 * Copyright (c) 2012-2020, FocalTech Systems, Ltd.
 */
/*
 * Nothing Phone (3a) probe 5: bring up the FocalTech touch controller by hand
 * and read its chip ID. ABL leaves the whole path off (probe 4): pins not
 * muxed, rails not voted, chip in reset, QUP0 SE0 unclocked. In order:
 *   1 TLMM   GPIO 20 reset low (out), GPIO 19 irq in + pull-up,
 *            GPIO 0-3 -> qup0_se0 (function 1), 6 mA, no pull (live DT)
 *   2 RPMh   ldob11 1800 mV, then ldob22 3300 mV (exact DT values), active
 *            votes through an idle ACTIVE TCS of apps RSC DRV2 (RSC v3.2
 *            register layout, rpmh-rsc.c rpmh_rsc_reg_offset_ver_3_0)
 *   3 GCC    vote QUP wrap0 core/2x/m_ahb/s_ahb/SE0 (0x52008 bits 18-22)
 *   4 reset  2 ms after power, GPIO 20 high, 200 ms (focaltech_core.c)
 *   5 GENI   SE0 must run SPI firmware; FIFO mode, mode 0, 8 bit, DFS
 *            level 0 (19.2 MHz) / 8 = 2.4 MHz (spi-geni-qcom.c)
 *   6 FTS    read 0xA3 / 0x9F (chip ID: 56 72 FT3683G, 56 62 FT3681) with
 *            the SPI v2 framing + CRC (focaltech_spi_v2.c)
 * Each step prints before the next one runs. No storage access.
 *
 * Probe 8 (-DPROBE8) is the same bring-up with SE0 in GSI mode: GENI's
 * FIFO interface is disabled on SE0, so SPI frames go through GPI DMA
 * (gpi.c). After the chip ID it polls the touch report (reg 0x01, SPI
 * protocol v2 layout, focaltech_core.c) and draws each finger on screen.
 */
#include <stdint.h>
#include "common.h"
#include "fbcon.h"
#ifdef PROBE8
#include "gpi.h"
#endif

/* ---- TLMM ---- */
#define TLMM_BASE	0x0f100000UL
#define TLMM_CFG(n)	(TLMM_BASE + (n) * 0x1000UL)
#define TLMM_IO(n)	(TLMM_BASE + (n) * 0x1000UL + 4)
#define CFG_PULL_DOWN	1U
#define CFG_PULL_UP	3U
#define CFG_FUNC(f)	((uint32_t)(f) << 2)
#define CFG_DRV_MA(ma)	((uint32_t)((ma) / 2 - 1) << 6)
#define CFG_OE		(1U << 9)
#define IO_OUT		(1U << 1)
#define PIN_IRQ		19
#define PIN_RESET	20

/* ---- GCC ---- */
#define GCC_BASE	0x00100000UL
#define GCC_VOTE	0x52008
#define GCC_QUP0_BITS	(0x1fU << 18)	/* core 2x, core, m_ahb, s_ahb, s0 */
#define GCC_S0_CBCR	0x18004
#define GCC_CORE_CBCR	0x23008
#define CBCR_OFF	(1U << 31)

/* ---- RPMh (apps RSC DRV2, v3 layout) ---- */
#define RSC_DRV2	0x17a20000UL
#define TCS_BASE	(RSC_DRV2 + 0xd00)
#define TCS_STRIDE	0x2a0
#define CMD_STRIDE	24
#define R_IRQ_EN	0x00
#define R_IRQ_STATUS	0x04
#define R_IRQ_CLEAR	0x08
#define R_WAIT		0x20
#define R_CONTROL	0x24
#define R_STATUS	0x28
#define R_ENABLE	0x2c
#define R_MSGID		0x34
#define R_ADDR		0x38
#define R_DATA		0x3c
#define R_CMD_STATUS	0x40
#define AMC_EN		(1U << 16)
#define AMC_TRIGGER	(1U << 24)
#define CMD_ISSUED	(1U << 8)
#define CMD_COMPL	(1U << 16)
#define MSGID_WRITE_ACK	(8U | (1U << 8) | (1U << 16))
#define N_ACTIVE	3	/* DT tcs-config: ACTIVE 3 first */
#define CMD_DB		0x81c60000UL
#define CMD_DB_SIZE	0x20000U
#define DB_MAGIC	0x0c0330dbU

/* ---- GENI SE0 (SPI) ---- */
#define SE		0x00a80000UL
#define GENI_FORCE_DEFAULT_REG	0x20
#define GENI_OUTPUT_CTRL	0x24
#define GENI_CGC_CTRL		0x28
#define GENI_STATUS		0x40
#define GENI_SER_M_CLK_CFG	0x48
#define GENI_IF_DISABLE_RO	0x64
#define GENI_FW_REVISION_RO	0x68
#define SE_GENI_CLK_SEL		0x7c
#define SE_SPI_CPHA		0x224
#define SE_SPI_LOOPBACK		0x22c
#define SE_SPI_CPOL		0x230
#define SE_SPI_DEMUX_OUTPUT_INV	0x24c
#define SE_SPI_DEMUX_SEL	0x250
#define SE_GENI_BYTE_GRAN	0x254
#define SE_GENI_DMA_MODE_EN	0x258
#define SE_SPI_TRANS_CFG	0x25c
#define SE_GENI_TX_PACKING_CFG0	0x260
#define SE_GENI_TX_PACKING_CFG1	0x264
#define SE_SPI_WORD_LEN		0x268
#define SE_SPI_TX_TRANS_LEN	0x26c
#define SE_SPI_RX_TRANS_LEN	0x270
#define SE_GENI_RX_PACKING_CFG0	0x284
#define SE_GENI_RX_PACKING_CFG1	0x288
#define SE_GENI_M_CMD0		0x600
#define SE_GENI_M_IRQ_STATUS	0x610
#define SE_GENI_M_IRQ_EN	0x614
#define SE_GENI_M_IRQ_CLEAR	0x618
#define SE_GENI_S_IRQ_EN	0x644
#define SE_GENI_S_IRQ_CLEAR	0x648
#define SE_GENI_TX_FIFOn	0x700
#define SE_GENI_RX_FIFOn	0x780
#define SE_GENI_TX_FIFO_STATUS	0x800
#define SE_GENI_RX_FIFO_STATUS	0x804
#define SE_GENI_RX_WATERMARK	0x810
#define SE_GENI_RX_RFR_WM	0x814
#define SE_GSI_EVENT_EN		0xe18
#define SE_IRQ_EN		0xe1c
#define SE_HW_PARAM_0		0xe24
#define SE_DMA_GENERAL_CFG	0xe30
#define M_CMD_DONE		(1U << 0)
#define M_OPCODE_SHFT		27
#define SPI_TX_RX		7
#define SPI_CS_ASSERT		8
#define SPI_CS_DEASSERT		9
#define FRAGMENTATION		(1U << 2)
#define CS_TOGGLE		(1U << 1)
#define PROTO_SPI		1


static void gpio_cfg(unsigned n, uint32_t cfg) { wr32(TLMM_CFG(n), cfg); }
static void gpio_out(unsigned n, int v) { wr32(TLMM_IO(n), v ? IO_OUT : 0); }

static int wait_bits(uintptr_t a, uint32_t mask, uint32_t want, unsigned us)
{
	uint64_t end = now() + rsr("cntfrq_el0") / 1000000 * us;
	while ((rd32(a) & mask) != want)
		if (now() > end)
			return -1;
	return 0;
}

/* ---- RPMh ---- */

static uint16_t db16(uint32_t off)
{
	return (uint16_t)(rd32(CMD_DB + (off & ~3U)) >> (8 * (off & 3U)));
}

static uint32_t db_lookup(const char *res)
{
	uint32_t key[2] = { 0, 0 }, found = 0, n = 0;
	for (unsigned i = 0; i < 8 && res[i]; i++)
		key[i / 4] |= (uint32_t)(uint8_t)res[i] << (8 * (i % 4));
	for (unsigned i = 0; i < 8; i++) {
		uint32_t h = 8 + 16 * i;
		unsigned slave = db16(h), count = db16(h + 6);
		uint32_t entries = 144 + db16(h + 2);
		if (!slave)
			break;
		if (entries + count * 24 > CMD_DB_SIZE)
			return 0;
		for (unsigned j = 0; j < count; j++) {
			uint32_t e = CMD_DB + entries + 24 * j;
			if (rd32(e) == key[0] && rd32(e + 4) == key[1]) {
				found = rd32(e + 16);
				n++;
			}
		}
	}
	/* exactly one VRM (slave 4) resource, 16-byte aligned */
	return n == 1 && (found >> 16) == 4 && !(found & 15) ? found : 0;
}

static uint32_t tcs_rd(unsigned t, uint32_t r) { return rd32(TCS_BASE + t * TCS_STRIDE + r); }
static void tcs_wr(unsigned t, uint32_t r, uint32_t v)
{
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(TCS_BASE + t * TCS_STRIDE + r, v);
	__asm__ volatile("dsb sy" ::: "memory");
}

static int tcs_idle(unsigned t)
{
	return tcs_rd(t, R_STATUS) != 0 && tcs_rd(t, R_CONTROL) == 0 &&
	       tcs_rd(t, R_ENABLE) == 0 && tcs_rd(t, R_WAIT) == 0 &&
	       !(rd32(TCS_BASE + R_IRQ_STATUS) & (1U << t));
}

/* One active (AMC) VRM request: voltage, mode HPM (7), enable; as sweet_rpmh.c. */
static int vreg_on(const char *label, const char *res, uint32_t mv)
{
	uint32_t addr = db_lookup(res);
	int t = -1;
	for (unsigned i = 0; i < N_ACTIVE && t < 0; i++)
		if (tcs_idle(i))
			t = (int)i;
	con_hex2(label, "addr=", addr, "tcs=", (uint32_t)t);
	if (!addr || t < 0)
		return -1;

	uint32_t bit = 1U << t, irq_en = rd32(TCS_BASE + R_IRQ_EN);
	wr32(TCS_BASE + R_IRQ_EN, irq_en & ~bit);
	static const uint32_t off[3] = { 0, 8, 4 };
	uint32_t val[3] = { mv, 7, 1 };
	for (unsigned c = 0; c < 3; c++) {
		tcs_wr(t, R_MSGID + c * CMD_STRIDE, MSGID_WRITE_ACK);
		tcs_wr(t, R_ADDR + c * CMD_STRIDE, addr + off[c]);
		tcs_wr(t, R_DATA + c * CMD_STRIDE, val[c]);
	}
	tcs_wr(t, R_WAIT, 7);
	tcs_wr(t, R_ENABLE, 7);
	tcs_wr(t, R_CONTROL, 0);
	tcs_wr(t, R_CONTROL, AMC_EN);
	tcs_wr(t, R_CONTROL, AMC_EN | AMC_TRIGGER);

	uint32_t st[3] = { 0, 0, 0 }, done = 0;
	uint64_t end = now() + rsr("cntfrq_el0") / 10;	/* 100 ms */
	while (now() < end) {
		done = 1;
		for (unsigned c = 0; c < 3; c++) {
			st[c] = tcs_rd(t, R_CMD_STATUS + c * CMD_STRIDE);
			if ((st[c] & (CMD_ISSUED | CMD_COMPL)) != (CMD_ISSUED | CMD_COMPL))
				done = 0;
		}
		if (done && tcs_rd(t, R_STATUS))
			break;
		done = 0;
	}
	con_hex2(" done", "ok=", done, "irq=", rd32(TCS_BASE + R_IRQ_STATUS));
	con_hex2(" cmd", "s0=", st[0], "s2=", st[2]);
	if (!done)
		return -1;
	tcs_wr(t, R_CONTROL, AMC_EN);
	tcs_wr(t, R_CONTROL, 0);
	tcs_wr(t, R_ENABLE, 0);
	tcs_wr(t, R_WAIT, 0);
	wr32(TCS_BASE + R_IRQ_CLEAR, bit);
	wr32(TCS_BASE + R_IRQ_EN, (rd32(TCS_BASE + R_IRQ_EN) & ~bit) | (irq_en & bit));
	return 0;
}

/* ---- GENI SPI ---- */

/* geni_se_config_packing(se, 8, 4, msb_to_lsb = true, tx, rx) */
static void packing_8bit(void)
{
	uint32_t cfg[4];
	for (int i = 0; i < 4; i++)
		cfg[i] = (uint32_t)(i * 8 + 7) << 5 | 1U << 4 | 7U << 1;
	cfg[3] |= 1;	/* stop */
	uint32_t c0 = cfg[0] | cfg[1] << 10, c1 = cfg[2] | cfg[3] << 10;
	wr32(SE + SE_GENI_TX_PACKING_CFG0, c0);
	wr32(SE + SE_GENI_TX_PACKING_CFG1, c1);
	wr32(SE + SE_GENI_RX_PACKING_CFG0, c0);
	wr32(SE + SE_GENI_RX_PACKING_CFG1, c1);
	wr32(SE + SE_GENI_BYTE_GRAN, 0);
}

static int spi_init(void)
{
	uint32_t fw = rd32(SE + GENI_FW_REVISION_RO), hw0 = rd32(SE + SE_HW_PARAM_0);
	uint32_t depth = (hw0 >> 16) & 0x3f;
	con_hex2("SE0", "fw=", fw, "proto=", (fw >> 8) & 0xff);
	con_hex2(" hw", "p0=", hw0, "ifdis=", rd32(SE + GENI_IF_DISABLE_RO));
#ifdef PROBE8
	if (((fw >> 8) & 0xff) != PROTO_SPI)
		return -1;
	/* geni_se_init() io part, then geni_se_select_gpi_mode() */
	wr32(SE + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	wr32(SE + SE_GENI_S_IRQ_CLEAR, 0xffffffff);
	wr32(SE + GENI_CGC_CTRL, rd32(SE + GENI_CGC_CTRL) | 0x7f);
	wr32(SE + SE_DMA_GENERAL_CFG, rd32(SE + SE_DMA_GENERAL_CFG) | 0xf);
	wr32(SE + GENI_OUTPUT_CTRL, 0x7f);
	wr32(SE + GENI_FORCE_DEFAULT_REG, 1);
	wr32(SE + SE_SPI_LOOPBACK, 0);
	wr32(SE + SE_SPI_DEMUX_SEL, 0);
	wr32(SE + SE_SPI_DEMUX_OUTPUT_INV, 0);
	wr32(SE + SE_IRQ_EN, 0);
	wr32(SE + SE_GENI_M_IRQ_EN, rd32(SE + SE_GENI_M_IRQ_EN) &
	     ~(M_CMD_DONE | (1U << 26) | (1U << 27) | (1U << 30)));
	wr32(SE + SE_GENI_DMA_MODE_EN, 1);
	wr32(SE + SE_GSI_EVENT_EN, rd32(SE + SE_GSI_EVENT_EN) | 0xf);
	con_hex2(" gsi", "dma=", rd32(SE + SE_GENI_DMA_MODE_EN), "ev=", rd32(SE + SE_GSI_EVENT_EN));
	return 0;
#endif
	if (((fw >> 8) & 0xff) != PROTO_SPI || depth < 4)
		return -1;
	/* geni_se_init(): irq clear, io init, io set mode, watermarks */
	wr32(SE + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	wr32(SE + SE_GENI_S_IRQ_CLEAR, 0xffffffff);
	wr32(SE + GENI_CGC_CTRL, rd32(SE + GENI_CGC_CTRL) | 0x7f);
	wr32(SE + SE_DMA_GENERAL_CFG, rd32(SE + SE_DMA_GENERAL_CFG) | 0xf);
	wr32(SE + GENI_OUTPUT_CTRL, 0x7f);
	wr32(SE + GENI_FORCE_DEFAULT_REG, 1);
	wr32(SE + SE_IRQ_EN, rd32(SE + SE_IRQ_EN) | 0xf);
	wr32(SE + SE_GENI_DMA_MODE_EN, rd32(SE + SE_GENI_DMA_MODE_EN) & ~1U);
	wr32(SE + SE_GSI_EVENT_EN, 0);
	wr32(SE + SE_GENI_RX_WATERMARK, depth - 3);
	wr32(SE + SE_GENI_RX_RFR_WM, depth - 2);
	wr32(SE + SE_GENI_M_IRQ_EN, rd32(SE + SE_GENI_M_IRQ_EN) | 0x7e |
	     M_CMD_DONE | (1U << 26) | (1U << 27) | (1U << 30));
	/* spi_geni_init() FIFO branch + setup_fifo_params(): mode 0, CS 0 */
	wr32(SE + SE_SPI_LOOPBACK, 0);
	wr32(SE + SE_SPI_DEMUX_SEL, 0);
	wr32(SE + SE_SPI_CPHA, 0);
	wr32(SE + SE_SPI_CPOL, 0);
	wr32(SE + SE_SPI_DEMUX_OUTPUT_INV, 0);
	wr32(SE + SE_SPI_TRANS_CFG, rd32(SE + SE_SPI_TRANS_CFG) & ~CS_TOGGLE);
	wr32(SE + SE_SPI_WORD_LEN, 8 - 4);
	packing_8bit();
	/* DFS level 0 = TCXO 19.2 MHz (probe 4: PERF_DFSR0 = 0x1); /8 = 2.4 MHz */
	wr32(SE + SE_GENI_CLK_SEL, 0);
	wr32(SE + GENI_SER_M_CLK_CFG, (8U << 4) | 1);
	con_hex2(" cfg", "depth=", depth, "status=", rd32(SE + GENI_STATUS));
	return 0;
}

static int m_cmd(uint32_t opcode, uint32_t params)
{
	wr32(SE + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	wr32(SE + SE_GENI_M_CMD0, opcode << M_OPCODE_SHFT | params);
	return 0;
}

static uint32_t last_mirq;

static int wait_done(void)
{
	int r = wait_bits(SE + SE_GENI_M_IRQ_STATUS, M_CMD_DONE, M_CMD_DONE, 50000);
	last_mirq = rd32(SE + SE_GENI_M_IRQ_STATUS);
	wr32(SE + SE_GENI_M_IRQ_CLEAR, last_mirq);
	return r;
}

/* Full-duplex transfer of n bytes (n <= 32), CS held for the whole frame. */
static int spi_xfer(const uint8_t *tx, uint8_t *rx, unsigned n)
{
#ifdef PROBE8
	int g = gpi_spi_xfer(tx, rx, n);
	last_mirq = gpi_status();
	delay_ms(1);	/* CS_HIGH_DELAY 150 us */
	return g;
#endif
	m_cmd(SPI_CS_ASSERT, 0);
	if (wait_done())
		return -1;
	wr32(SE + SE_SPI_TX_TRANS_LEN, n);
	wr32(SE + SE_SPI_RX_TRANS_LEN, n);
	m_cmd(SPI_TX_RX, FRAGMENTATION);
	for (unsigned i = 0; i < n; i += 4) {
		uint32_t w = 0;
		for (unsigned j = 0; j < 4 && i + j < n; j++)
			w |= (uint32_t)tx[i + j] << (8 * j);
		wr32(SE + SE_GENI_TX_FIFOn, w);
	}
	unsigned got = 0;
	uint64_t end = now() + rsr("cntfrq_el0") / 20;
	while (got < n && now() < end) {
		uint32_t wc = rd32(SE + SE_GENI_RX_FIFO_STATUS) & 0x1ffffff;
		while (wc-- && got < n) {
			uint32_t w = rd32(SE + SE_GENI_RX_FIFOn);
			for (unsigned j = 0; j < 4 && got < n; j++)
				rx[got++] = (uint8_t)(w >> (8 * j));
		}
	}
	int r = wait_done();
	m_cmd(SPI_CS_DEASSERT, 0);
	wait_done();
	delay_ms(1);	/* CS_HIGH_DELAY 150 us */
	return got == n && !r ? 0 : -1;
}

/* focaltech_spi_v2.c crckermit */
static uint16_t crc_kermit(const uint8_t *d, unsigned n)
{
	uint16_t crc = 0xffff;
	for (unsigned i = 0; i < n; i++) {
		crc ^= d[i];
		for (int j = 0; j < 8; j++)
			crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
	}
	return crc;
}

/* fts_read(): [addr, 0xA0, len hi, len lo, 3 dummy, data..., crc lo, crc hi] */
static int fts_read_reg(const char *label, uint8_t reg, uint8_t *val)
{
	uint8_t tx[10] = { reg, 0xa0, 0, 1 }, rx[10] = { 0 };
	int r = spi_xfer(tx, rx, sizeof(tx));
	uint16_t crc = (uint16_t)(rx[9] << 8 | rx[8]);
	int ok = !r && crc == crc_kermit(rx + 7, 1) && !(rx[3] & 0xa0);
	con_hex2(label, "rx0-3=", rx[0] << 24 | rx[1] << 16 | rx[2] << 8 | rx[3],
		 "d/crc=", rx[7] << 16 | crc);
	con_hex2(" ", "ok=", (uint32_t)ok, "mirq=", last_mirq);
	*val = rx[7];
	return ok ? 0 : -1;
}

#ifdef PROBE8
#define TOUCH_LEN	84	/* FTS_TOUCH_DATA_LEN_V2: 10 points x 8 + 4 */
#define FTS_UP		1	/* event flag: 0 down, 1 up, 2 contact */
#define DOT		24

/* fts_read() of len bytes at reg, CRC checked; -2 status, -3 CRC */
static int fts_read_buf(uint8_t reg, uint8_t *buf, unsigned len)
{
	static uint8_t tx[GPI_BUF], rx[GPI_BUF];
	unsigned n = 7 + len + 2;
	for (unsigned i = 0; i < n; i++)
		tx[i] = 0;
	tx[0] = reg;
	tx[1] = 0xa0;
	tx[2] = (uint8_t)(len >> 8);
	tx[3] = (uint8_t)len;
	if (spi_xfer(tx, rx, n))
		return -1;
	if (rx[3] & 0xa0)
		return -2;
	if ((uint16_t)(rx[n - 1] << 8 | rx[n - 2]) != crc_kermit(rx + 7, len))
		return -3;
	for (unsigned i = 0; i < len; i++)
		buf[i] = rx[7 + i];
	return 0;
}

static void dot(uint32_t x, uint32_t y, uint32_t c)
{
	for (uint32_t dy = 0; dy < DOT; dy++)
		for (uint32_t dx = 0; dx < DOT; dx++) {
			uint32_t px = x + dx - DOT / 2, py = y + dy - DOT / 2;
			if (px < fb.w && py < fb.h && fb.bpp == 4)
				wr32(fb.base + py * fb.stride + px * 4, c);
		}
}

/*
 * Read the report on every IRQ-low sample (GPIO 19, falling edge in DT) and
 * every 50 ms regardless. Fingers leave dots: yellow id 0, white others.
 * Decoding as fts_input_report_touch_pv2() / fts_input_report_touch().
 */
static void __attribute__((noreturn)) touch_loop(void)
{
	static uint8_t b[TOUCH_LEN];
	uint64_t f = rsr("cntfrq_el0"), next_poll = 0, next_pet = 0, next_draw = 0;
	uint32_t reads = 0, errs = 0, lows = 0, touches = 0, last_err = 0;
	uint32_t pt[2] = { 0, 0 };
	int top = con_row();

	for (;;) {
		uint64_t t = now();
		int low = !(rd32(TLMM_IO(PIN_IRQ)) & 1);
		if (t >= next_pet) {
			smc(GH_WDT_PET, 0, 0, 0);
			next_pet = t + f;
		}
		if (!low && t < next_poll)
			continue;
		next_poll = t + f / 20;
		lows += low;
		reads++;
		int r = fts_read_buf(0x01, b, TOUCH_LEN);
		if (r) {
			errs++;
			last_err = (uint32_t)-r << 24 | last_mirq;
		} else if (!(b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff)) {
			unsigned type = b[1] >> 4, num = b[1] & 0xf;
			for (unsigned i = 0; i < 10; i++) {
				unsigned base, id, flag;
				uint32_t x, y;
				if (type == 2) {
					if (i >= num)
						break;
					base = 4 + 8 * i;
					x = ((b[base] & 0xfU) << 12 | (uint32_t)b[base + 1] << 4 |
					     b[base + 4] >> 4) / 16;
					y = ((b[base + 2] & 0xfU) << 12 | (uint32_t)b[base + 3] << 4 |
					     (b[base + 4] & 0xfU)) / 16;
				} else if (type == 0) {
					base = 2 + 6 * i;
					if ((b[base + 2] >> 4) >= 10)
						break;
					x = (b[base] & 0xfU) << 8 | b[base + 1];
					y = (b[base + 2] & 0xfU) << 8 | b[base + 3];
				} else {
					break;
				}
				id = b[base + 2] >> 4;
				flag = b[base] >> 6;
				if (i < 2)
					pt[i] = id << 28 | flag << 24 | x << 12 | y;
				if (flag != FTS_UP) {
					dot(x, y, id ? WHITE : YELLOW);
					touches++;
				}
			}
		}
		if (t < next_draw)
			continue;
		next_draw = t + f / 5;
		con_set_row(top);
		con_hex2("TOUCH", "reads=", reads, "errs=", errs);
		con_hex2(" ", "irqlow=", lows, "dots=", touches);
		con_hex2(" raw", "b0-3=", (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3],
			 "b4-7=", (uint32_t)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7]);
		con_hex2(" pts", "p0=", pt[0], "p1=", pt[1]);
		con_hex2(" ", "lasterr=", last_err, "g19=", rd32(TLMM_IO(PIN_IRQ)));
	}
}
#endif

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
#ifdef PROBE8
	con_str("PROBE", "asteroids probe 8 (touch, GPI)");
#else
	con_str("PROBE", "asteroids probe 5 (touch)");
#endif
	status_row = con_row();
	con_skip();
	con_skip();
	con_skip();
	con_skip();

	stage = "tlmm";
	gpio_out(PIN_RESET, 0);
	gpio_cfg(PIN_RESET, CFG_OE);			/* out, 2 mA, no pull, low */
	gpio_cfg(PIN_IRQ, CFG_PULL_UP);			/* in, pull-up */
	for (unsigned n = 0; n < 4; n++)
		gpio_cfg(n, CFG_FUNC(1) | CFG_DRV_MA(6));
	con_hex2("TLMM", "g0=", rd32(TLMM_CFG(0)), "g20=", rd32(TLMM_CFG(PIN_RESET)));
	con_hex2(" io", "g19=", rd32(TLMM_IO(PIN_IRQ)), "g20=", rd32(TLMM_IO(PIN_RESET)));

	stage = "rpmh";
	con_hex2("RSC", "id=", rd32(RSC_DRV2), "irqen=", rd32(TCS_BASE + R_IRQ_EN));
	if (rd32(CMD_DB + 4) != DB_MAGIC || ((rd32(RSC_DRV2) >> 16) & 0xff) != 3) {
		con_str("RPMH", "unexpected RSC/cmd-db: stop");
		goto out;
	}
	if (vreg_on("ldob11", "ldob11", 1800) || vreg_on("ldob22", "ldob22", 3300)) {
		con_str("RPMH", "vote failed: stop");
		goto out;
	}
	smc(GH_WDT_PET, 0, 0, 0);

	stage = "gcc";
	wr32(GCC_BASE + GCC_VOTE, rd32(GCC_BASE + GCC_VOTE) | GCC_QUP0_BITS);
	int clk = wait_bits(GCC_BASE + GCC_S0_CBCR, CBCR_OFF, 0, 10000) |
		  wait_bits(GCC_BASE + GCC_CORE_CBCR, CBCR_OFF, 0, 10000);
	con_hex2("GCC", "vote=", rd32(GCC_BASE + GCC_VOTE), "s0=", rd32(GCC_BASE + GCC_S0_CBCR));
	if (clk) {
		con_str("GCC", "SE0 clock did not start: stop");
		goto out;
	}

	stage = "reset";
	delay_ms(2);
	gpio_out(PIN_RESET, 1);
	delay_ms(200);
	con_hex2("RESET", "g20io=", rd32(TLMM_IO(PIN_RESET)), "g19io=", rd32(TLMM_IO(PIN_IRQ)));

	stage = "geni";
	if (spi_init()) {
		con_str("GENI", "SE0 is not SPI / no FIFO: stop");
		goto out;
	}
#ifdef PROBE8
	stage = "gpi";
	if (gpi_init()) {
		gpi_dump();
		con_str("GPI", "channel setup failed: stop");
		goto out;
	}
	/* run 2 reset the phone at the first doorbell: show the SMMU first */
	stage = "smmu";
	gpi_smmu_check();
	int cd = con_row();
	for (unsigned s = 15; s > 0; s--) {
		con_set_row(cd);
		con_dec("DOORBELL in", s);
		smc(GH_WDT_PET, 0, 0, 0);
		delay_ms(1000);
	}
	con_set_row(cd);
	con_str("DOORBELL", "now                ");
#endif

	stage = "fts";
	uint8_t id1 = 0, id2 = 0, fw = 0;
	fts_read_reg("ID 0xA3", 0xa3, &id1);
	fts_read_reg("ID 0x9F", 0x9f, &id2);
	fts_read_reg("FW 0xA6", 0xa6, &fw);
	con_hex2("CHIP", "a3/9f=", id1 << 8 | id2, "fw=", fw);
	con_hex2(" irq", "g19io=", rd32(TLMM_IO(PIN_IRQ)), 0, 0);
#ifdef PROBE8
	if (id1 != 0x56)	/* no FocalTech answer: show where GPI stands */
		gpi_dump();
	stage = "touch";
	con_str("TOUCH", "touch the screen; vol- + power exits");
	touch_loop();
#endif

out:
	stage = "done";
	con_skip();
	con_str("DONE", "photo this, then hold");
	con_str(" ", "power + vol- for fastboot");
	hold();
}
