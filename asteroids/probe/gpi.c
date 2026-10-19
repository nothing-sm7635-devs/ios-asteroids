// SPDX-License-Identifier: GPL-2.0-only
/*
 * Derived from GPL-2.0 sources in the Nothing Phone (3a) kernel tree
 * (sm7635, see asteroids/CREDITS.md):
 *   GPII register map, channel/event ring setup,   drivers/dma/qcom/msm_gpi.c,
 *   command sequence, event layout                 drivers/dma/qcom/msm_gpi_mmio.h
 *   TRE formats                                    include/linux/msm_gpi.h
 *   SPI config0/go/dma TRE use                     drivers/spi/spi-msm-geni.c
 * Copyright (c) 2017-2021, The Linux Foundation.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc.
 */
/*
 * Minimal polled GPI DMA for QUP0 SE0 (SPI), which has its FIFO interface
 * disabled (probe 5). Live DT: gpi-dma@a00000, gpi-ee-offset 0x10000,
 * static-gpii-mask 0x1; SE0 dmas = <gpi 0 0 1 64 2>, <gpi 1 0 1 64 2>:
 * channel 0 TX / 1 RX, SE id 0, protocol 1 (SPI), args[4] = 2 -> static
 * GPII 0. No interrupts are taken: the GPII interrupt type/source registers
 * are polled, exactly where msm_gpi.c waits for its IRQ. Rings and buffers
 * are in the image (MMU off, so uncached and VA == PA). DMA goes out with
 * SID 0x576; this assumes the apps SMMU passes unconfigured streams through
 * as it does for UFS (probe 2).
 */
#include <stdint.h>
#include "common.h"
#include "fbcon.h"
#include "gpi.h"

#define EE		(0x00a00000UL - 0x10000)	/* gpi-top - gpi-ee-offset */
#define GPII		0
#define G(o)		(EE + (o) + 0x4000UL * GPII)
#define CH_CNTXT(k)	G(0x20000 + 0x80 * (k))
#define CH_SCRATCH(k)	G(0x20060 + 0x80 * (k))
#define CH_QOS(k)	G(0x2005c + 0x80 * (k))
#define EV_CNTXT	G(0x21000)
#define CH_DB(k)	G(0x22000 + 0x8 * (k))
#define EV_DB		G(0x22100)
#define CH_CMD		G(0x23008)
#define EV_CMD		G(0x23010)
#define TYPE_IRQ	G(0x23080)
#define TYPE_IRQ_MSK	G(0x23088)
#define SRC_CH_IRQ	G(0x23090)
#define SRC_EV_IRQ	G(0x23094)
#define SRC_CH_MSK	G(0x23098)
#define SRC_EV_MSK	G(0x2309c)
#define SRC_CH_CLR	G(0x230a0)
#define SRC_EV_CLR	G(0x230a4)
#define IEOB_MSK	G(0x230b8)
#define IEOB_CLR	G(0x230c0)
#define GLOB_STTS	G(0x23100)
#define GLOB_EN		G(0x23108)
#define GPII_STTS	G(0x23118)
#define GPII_EN		G(0x23120)
#define INTSET		G(0x23180)
#define MSI_LSB		G(0x23188)
#define MSI_MSB		G(0x2318c)
#define SCRATCH0	G(0x23400)
#define SCRATCH1	G(0x23404)
#define ERROR_LOG	G(0x23200)
/* context words */
#define C_CONFIG	0x00
#define C_LEN		0x04
#define C_BASE_LO	0x08
#define C_BASE_HI	0x0c
#define C_RP_LO		0x10
#define C_INT_MOD	0x20
#define C_MSI_LO	0x28
#define C_MSI_HI	0x2c
#define C_RPU_LO	0x30
#define C_RPU_HI	0x34

#define T_CH_CTRL	0x01
#define T_EV_CTRL	0x02
#define T_GLOB		0x04
#define T_IEOB		0x08
#define T_GENERAL	0x40

#define OP_ALLOC	0
#define OP_START	1
#define OP_STOP		2
#define OP_RESET	9
#define OP_DEALLOC	10
#define ST_ALLOCATED	1
#define ST_STARTED	2

#define EV_XFER_COMPL	0x22
#define TCE_SUCCESS	1
#define TCE_EOT		2

/* SPI TRE fields (msm_gpi.h, spi-msm-geni.c) */
#define SPI_FULL_DUPLEX	3
#define WORD_LEN	(8 - 4)
#define PACK_TX_RX	3
#define CLK_DIV		20	/* DFS level 0 (19.2 MHz TCXO) / 20 ~ 1 MHz */

#define RING_N		16
struct tre {
	uint32_t d[4];
};

static struct tre tx_ring[RING_N] __attribute__((aligned(RING_N * 16)));
static struct tre rx_ring[RING_N] __attribute__((aligned(RING_N * 16)));
static struct tre ev_ring[RING_N] __attribute__((aligned(RING_N * 16)));
static uint8_t tx_buf[GPI_BUF] __attribute__((aligned(64)));
static uint8_t rx_buf[GPI_BUF] __attribute__((aligned(64)));
static unsigned tx_wp, rx_wp, ev_rp, ev_wp, n_ev;
static int configured;
static uint32_t last_ev[4], last_codes, bad_rp, n_xfer;

static uint32_t pa(const void *p) { return (uint32_t)(uintptr_t)p; }
static uint32_t state(uintptr_t cntxt) { return (rd32(cntxt + C_CONFIG) >> 20) & 0xf; }

/* gpi_send_cmd(), polling the CH_CTRL / EV_CTRL interrupt instead of waiting */
static int gpi_cmd(int ev, unsigned chid, uint32_t op, int want)
{
	uint32_t bit = ev ? T_EV_CTRL : T_CH_CTRL;
	wr32(ev ? EV_CMD : CH_CMD, op << 24 | (ev ? 0 : chid));
	uint64_t end = now() + rsr("cntfrq_el0") / 4;	/* CMD_TIMEOUT_MS 250 */
	int irq = 0;
	while (!irq && now() < end)
		irq = (rd32(TYPE_IRQ) & bit) != 0;
	if (irq) {
		uint32_t src = rd32(ev ? SRC_EV_IRQ : SRC_CH_IRQ);
		wr32(ev ? SRC_EV_CLR : SRC_CH_CLR, src);
	}
	uint32_t st = state(ev ? EV_CNTXT : CH_CNTXT(chid));
	if (want >= 0 && st != (uint32_t)want) {
		con_hex2(ev ? "GPI evcmd" : "GPI chcmd", "op/ch=", op << 8 | chid,
			 "irq/st=", (uint32_t)irq << 8 | st);
		return -1;
	}
	return 0;
}

static void ring_ctx(uintptr_t cntxt, uintptr_t db, const struct tre *ring, uint32_t cfg)
{
	wr32(cntxt + C_CONFIG, cfg);
	wr32(cntxt + C_LEN, sizeof(struct tre) * RING_N);
	wr32(cntxt + C_BASE_LO, pa(ring));
	wr32(cntxt + C_BASE_HI, 0);
	wr32(db + 4, 0);	/* doorbell MSB = ring base MSB */
}

void gpi_dump(void)
{
	con_hex2("GPII0", "ch0=", rd32(CH_CNTXT(0)), "ch1=", rd32(CH_CNTXT(1)));
	con_hex2(" ", "ev=", rd32(EV_CNTXT), "type=", rd32(TYPE_IRQ));
	con_hex2(" ", "glob=", rd32(GLOB_STTS), "gpii=", rd32(GPII_STTS));
	con_hex2(" ", "errlog=", rd32(ERROR_LOG), "nev=", n_ev);
	con_hex2(" rp", "ch0=", rd32(CH_CNTXT(0) + C_RP_LO), "ch1=", rd32(CH_CNTXT(1) + C_RP_LO));
	con_hex2(" rp", "ev=", rd32(EV_CNTXT + C_RP_LO), "ring=", pa(ev_ring));
	con_hex2(" lastev", "d2=", last_ev[2], "d3=", last_ev[3]);
	con_hex2(" ", "badrp=", bad_rp, "xfers=", n_xfer);
}

int gpi_init(void)
{
	con_hex2("GPII0", "ch0=", rd32(CH_CNTXT(0)), "ch1=", rd32(CH_CNTXT(1)));
	con_hex2(" ", "ev=", rd32(EV_CNTXT), "type=", rd32(TYPE_IRQ));

	/* gpi_config_interrupts(DEFAULT_IRQ_SETTINGS); the GIC SPI stays off */
	wr32(TYPE_IRQ_MSK, T_GENERAL | T_IEOB | T_GLOB | T_EV_CTRL | T_CH_CTRL);
	wr32(IEOB_MSK, 1);
	wr32(SRC_CH_MSK, 3);
	wr32(SRC_EV_MSK, 1);
	wr32(GLOB_EN, 1);
	wr32(GPII_EN, 0xf);
	wr32(MSI_LSB, 0);
	wr32(MSI_MSB, 0);
	wr32(SCRATCH0, 0);
	wr32(SCRATCH1, 0);
	wr32(INTSET, 1);
	wr32(ERROR_LOG, 0);

	/* left allocated by an earlier owner: gpi_cleanup_hw_channel() */
	for (unsigned k = 0; k < 2; k++)
		if (state(CH_CNTXT(k))) {
			con_hex2("GPI", "cleanup ch=", k, "st=", state(CH_CNTXT(k)));
			gpi_cmd(0, k, OP_STOP, -1);
			gpi_cmd(0, k, OP_RESET, -1);
			gpi_cmd(0, k, OP_DEALLOC, -1);
		}
	if (state(EV_CNTXT))
		gpi_cmd(1, 0, OP_DEALLOC, -1);

	/* gpi_alloc_ev_chan() */
	if (gpi_cmd(1, 0, OP_ALLOC, ST_ALLOCATED))
		return -1;
	ring_ctx(EV_CNTXT, EV_DB, ev_ring, 16U << 24 | 1U << 16 | 2);	/* el 16, IRQ, GPI_EV */
	wr32(EV_CNTXT + C_INT_MOD, 0);
	wr32(EV_CNTXT + C_MSI_LO, 0);
	wr32(EV_CNTXT + C_MSI_HI, 0);
	wr32(EV_CNTXT + C_RPU_LO, 0);
	wr32(EV_CNTXT + C_RPU_HI, 0);
	ev_rp = 0;
	ev_wp = RING_N - 1;
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(EV_DB, pa(&ev_ring[ev_wp]));

	/* gpi_alloc_chan(): ch 0 out (TX), ch 1 in (RX), protocol GPI */
	for (unsigned k = 0; k < 2; k++) {
		if (gpi_cmd(0, k, OP_ALLOC, ST_ALLOCATED))
			return -1;
		ring_ctx(CH_CNTXT(k), CH_DB(k), k ? rx_ring : tx_ring,
			 16U << 24 | (k ? 0U : 1U) << 3 | 2);
		/* pair = !chid, init_config 0, protocol 1 (SPI), SE id 0 */
		wr32(CH_SCRATCH(k), (k ? 0U : 1U) << 16 | 1U << 4 | 0);
		wr32(CH_SCRATCH(k) + 4, 0);
		wr32(CH_SCRATCH(k) + 8, 0);
		wr32(CH_SCRATCH(k) + 12, 0);
		wr32(CH_QOS(k), 1);
	}
	tx_wp = rx_wp = 0;
	__asm__ volatile("dsb sy" ::: "memory");
	for (unsigned k = 0; k < 2; k++)
		if (gpi_cmd(0, k, OP_START, ST_STARTED))
			return -1;
	con_hex2("GPI up", "ch0=", rd32(CH_CNTXT(0)), "ch1=", rd32(CH_CNTXT(1)));
	con_hex2(" ", "ev=", rd32(EV_CNTXT), "evrp=", rd32(EV_CNTXT + C_RP_LO));
	con_hex2(" ", "ring=", pa(ev_ring), "txring=", pa(tx_ring));
	return 0;
}

static void put_tre(struct tre *ring, unsigned *wp, uint32_t d0, uint32_t d1,
		    uint32_t d2, uint32_t d3)
{
	struct tre *t = &ring[*wp];
	t->d[0] = d0;
	t->d[1] = d1;
	t->d[2] = d2;
	t->d[3] = d3;
	*wp = (*wp + 1) % RING_N;
}

/* (n_ev << 16) | tx code << 8 | rx code of the last transfer */
uint32_t gpi_status(void)
{
	return last_codes;
}

int gpi_spi_xfer(const uint8_t *tx, uint8_t *rx, unsigned n)
{
	if (n > GPI_BUF)
		return -1;
	for (unsigned i = 0; i < n; i++) {
		tx_buf[i] = tx[i];
		rx_buf[i] = 0;
	}
	/* setup_config0_tre() once, setup_go_tre(), setup_dma_tre() */
	if (!configured) {
		put_tre(tx_ring, &tx_wp, PACK_TX_RX << 24 | 0 << 8 | WORD_LEN, 0,
			0U << 16 | CLK_DIV, 2U << 20 | 2U << 16 | 1);
		configured = 1;
	}
	put_tre(tx_ring, &tx_wp, 0U << 24 | 0U << 8 | SPI_FULL_DUPLEX, 0, n,
		2U << 20 | 0U << 16 | 1U << 11 | 1);		/* link rx, chain */
	put_tre(tx_ring, &tx_wp, pa(tx_buf), 0, n, 1U << 20 | 1U << 9);	/* ieot */
	put_tre(rx_ring, &rx_wp, pa(rx_buf), 0, n, 1U << 20);
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(CH_DB(0), pa(&tx_ring[tx_wp]));
	wr32(CH_DB(1), pa(&rx_ring[rx_wp]));

	if (!n_xfer++)
		con_hex2("GPI db", "evrp=", rd32(EV_CNTXT + C_RP_LO),
			 "chrp=", rd32(CH_CNTXT(0) + C_RP_LO));

	/* gpi_process_events(), polled */
	uint32_t txc = 0, rxc = 0;
	uint64_t end = now() + rsr("cntfrq_el0") / 20;	/* 50 ms */
	while (now() < end && !(txc && rxc)) {
		uint32_t hw = rd32(EV_CNTXT + C_RP_LO);
		int got = 0;
		/* an RP outside the ring would never match ev_rp */
		if (hw - pa(ev_ring) >= sizeof(ev_ring) || (hw & 15)) {
			bad_rp = hw;
			break;
		}
		while (pa(&ev_ring[ev_rp]) != hw) {
			const struct tre *e = &ev_ring[ev_rp];
			for (int i = 0; i < 4; i++)
				last_ev[i] = e->d[i];
			uint32_t type = (last_ev[3] >> 16) & 0xff, code = last_ev[2] >> 24;
			if (type == EV_XFER_COMPL) {
				if (last_ev[3] >> 24)
					rxc = code;
				else
					txc = code;
			}
			n_ev++;
			ev_rp = (ev_rp + 1) % RING_N;
			ev_wp = (ev_wp + 1) % RING_N;
			got = 1;
		}
		if (got) {
			wr32(EV_DB, pa(&ev_ring[ev_wp]));
			wr32(IEOB_CLR, 1);
		}
		/* the RX DMA TRE has no IEOT: done once the channel consumed it */
		if (txc && !rxc && rd32(CH_CNTXT(1) + C_RP_LO) == pa(&rx_ring[rx_wp]))
			rxc = 0xff;
	}
	last_codes = (n_ev & 0xffff) << 16 | (txc & 0xff) << 8 | (rxc & 0xff);
	for (unsigned i = 0; i < n; i++)
		rx[i] = rx_buf[i];
	return (txc == TCE_EOT || txc == TCE_SUCCESS) && rxc ? 0 : -1;
}
