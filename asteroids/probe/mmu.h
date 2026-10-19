#pragma once
#include <stdint.h>

struct mmu_info {
	uint64_t img_lo, img_hi, ttbr;
	uint64_t sctlr_before, sctlr_after;
	int n_l3;
};

int mmu_enable(struct mmu_info *info, uintptr_t fb_lo, uintptr_t fb_hi);
uint64_t mmu_probe(uintptr_t va);
