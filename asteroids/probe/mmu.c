/*
 * EL1 stage-1 MMU with the 16K granule XNU uses, as in shim/mmu.c, sized
 * for the Nothing Phone (3a): RAM reaches 0xa00000000, so the identity map
 * covers the whole 64GB a level-2-rooted walk allows (T0SZ=28).
 *
 * Under Gunyah, stage 2 decides what we may touch; stage 1 still keeps
 * everything Device except our own image (Normal WB) and the framebuffer
 * pages (Normal NC). Both get page-granular L3 tables, so no carve-out
 * next to them is ever mapped Normal (no speculative reads into it).
 */
#include <stdint.h>
#include "mmu.h"

#define PG_SHIFT	14
#define PG_SIZE		(1UL << PG_SHIFT)		/* 16K */
#define BLK_SHIFT	25
#define BLK_SIZE	(1UL << BLK_SHIFT)		/* 32MB level-2 block */
#define NENT		2048
#define NTABLES		6			/* L2 + up to 5 L3 */

#define ATTR_DEV	0	/* MAIR attr0 = 0x00 Device-nGnRnE */
#define ATTR_WB		1	/* MAIR attr1 = 0xff Normal WB RW-alloc */
#define ATTR_NC		2	/* MAIR attr2 = 0x44 Normal non-cacheable */
#define MAIR_VAL	0x44ff00UL

#define D_BLOCK		1UL
#define D_TABLE		3UL
#define D_PAGE		3UL
#define D_ATTR(i)	((uint64_t)(i) << 2)
#define D_SH_OUTER	(2UL << 8)
#define D_SH_INNER	(3UL << 8)
#define D_AF		(1UL << 10)
#define D_PXN		(1UL << 53)
#define D_UXN		(1UL << 54)

#define DEV_FLAGS	(D_ATTR(ATTR_DEV) | D_AF | D_PXN | D_UXN)
#define WB_FLAGS	(D_ATTR(ATTR_WB) | D_SH_INNER | D_AF | D_UXN)
#define NC_FLAGS	(D_ATTR(ATTR_NC) | D_SH_OUTER | D_AF | D_PXN | D_UXN)

/* TCR: T0SZ=28, IRGN0/ORGN0=WBWA, SH0=inner, TG0=16K, EPD1, TG1=16K, IPS=40 bit */
#define TCR_VAL		(28UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (2UL << 14) | \
			 (28UL << 16) | (1UL << 23) | (1UL << 30) | (2UL << 32))

#define SCTLR_M		(1UL << 0)
#define SCTLR_A		(1UL << 1)
#define SCTLR_C		(1UL << 2)
#define SCTLR_I		(1UL << 12)
#define SCTLR_WXN	(1UL << 19)

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))

extern char _head[], _end[];

static uint64_t pt_pool[(NTABLES + 1) * NENT] __attribute__((aligned(4096)));

static void dcache_inval_range(uintptr_t start, uintptr_t end)
{
	uint64_t line = 4UL << ((rsr("ctr_el0") >> 16) & 0xf);

	for (uintptr_t a = start & ~(line - 1); a < end; a += line)
		__asm__ volatile("dc ivac, %0" : : "r"(a) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static uintptr_t pool;
static int n_tables;

/* Give [lo, hi) page attributes `flags`, splitting its blocks into L3 tables. */
static int map_pages(uint64_t *l2, uintptr_t lo, uintptr_t hi, uint64_t flags)
{
	for (uint64_t blk = lo >> BLK_SHIFT; blk <= (hi - 1) >> BLK_SHIFT; blk++) {
		uint64_t *l3;
		if ((l2[blk] & 3) == D_TABLE) {
			l3 = (uint64_t *)(l2[blk] & ~(PG_SIZE - 1));
		} else {
			if (n_tables == NTABLES)
				return -1;
			l3 = (uint64_t *)(pool + n_tables++ * PG_SIZE);
			for (int i = 0; i < NENT; i++)
				l3[i] = ((blk << BLK_SHIFT) + ((uint64_t)i << PG_SHIFT)) | DEV_FLAGS | D_PAGE;
			l2[blk] = (uintptr_t)l3 | D_TABLE;
		}
		for (int i = 0; i < NENT; i++) {
			uint64_t pa = (blk << BLK_SHIFT) + ((uint64_t)i << PG_SHIFT);
			if (pa + PG_SIZE > lo && pa < hi)
				l3[i] = pa | flags | D_PAGE;
		}
	}
	return 0;
}

int mmu_enable(struct mmu_info *info, uintptr_t fb_lo, uintptr_t fb_hi)
{
	uintptr_t img_lo = (uintptr_t)_head & ~(PG_SIZE - 1);
	uintptr_t img_hi = ((uintptr_t)_end + PG_SIZE - 1) & ~(PG_SIZE - 1);
	uint64_t *l2;

	pool = ((uintptr_t)pt_pool + PG_SIZE - 1) & ~(PG_SIZE - 1);
	l2 = (uint64_t *)pool;
	n_tables = 1;
	info->img_lo = img_lo;
	info->img_hi = img_hi;
	info->ttbr = pool;

	for (int i = 0; i < NENT; i++)
		l2[i] = ((uint64_t)i << BLK_SHIFT) | DEV_FLAGS | D_BLOCK;
	if (map_pages(l2, img_lo, img_hi, WB_FLAGS) ||
	    map_pages(l2, fb_lo & ~(PG_SIZE - 1), fb_hi, NC_FLAGS))
		return -1;
	info->n_l3 = n_tables - 1;

	/* Image lines may be stale in the cache from ABL; we wrote memory directly. */
	dcache_inval_range(img_lo, img_hi);
	__asm__ volatile("ic iallu; dsb sy; isb" ::: "memory");

	wsr("mair_el1", MAIR_VAL);
	wsr("tcr_el1", TCR_VAL);
	wsr("ttbr0_el1", pool);
	__asm__ volatile("isb; tlbi vmalle1; dsb nsh; isb" ::: "memory");

	uint64_t sctlr = rsr("sctlr_el1");
	info->sctlr_before = sctlr;
	sctlr &= ~(SCTLR_A | SCTLR_WXN);
	sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;
	wsr("sctlr_el1", sctlr);
	__asm__ volatile("isb" ::: "memory");
	info->sctlr_after = rsr("sctlr_el1");
	return 0;
}

/* Translate a VA with AT S1E1R; returns PAR_EL1. */
uint64_t mmu_probe(uintptr_t va)
{
	__asm__ volatile("at s1e1r, %0; isb" : : "r"(va) : "memory");
	return rsr("par_el1");
}
