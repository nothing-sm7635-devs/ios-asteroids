/*
 * Act as iBoot for xnu on the Nothing Phone (3a), as shim/loader.c does on
 * sweet, with two differences:
 *   - the SWEETXNU payload is appended to our own Image (build.sh), right at
 *     _end: ABL ignores a boot.img v4 ramdisk here and builds the initrd from
 *     vendor_boot + the flashed init_boot (+ bootconfig) instead. The initrd
 *     is still searched as a fallback; payloads there sit at arbitrary byte
 *     offsets, so all copies use aligned loads only (the MMU is off, every
 *     access is Device memory and must be naturally aligned).
 *   - the framebuffer comes from the MDSS source pipe (fbcon.c), not a constant.
 */
#include <stdint.h>
#include "../probe/common.h"
#include "../probe/fbcon.h"
#include "../../shim/fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL
#define MAX_SEGS		16

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[MAX_SEGS];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/*
 * Copy n bytes from any src to an 8-byte aligned dst using only aligned
 * 8-byte loads and stores: each output word is spliced from two input words.
 * May read up to 15 bytes past src + n (still inside the ABL initrd area).
 */
static void copy_any(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	unsigned sh = (src & 7) * 8;
	volatile const uint64_t *s = (volatile const uint64_t *)(src & ~7UL);
	uint64_t words = (n + 7) / 8;

	if (!sh) {
		for (uint64_t i = 0; i < words; i++)
			d[i] = s[i];
		return;
	}
	uint64_t lo = s[0];
	for (uint64_t i = 0; i < words; i++) {
		uint64_t hi = s[i + 1];
		d[i] = lo >> sh | hi << (64 - sh);	/* little endian */
		lo = hi;
	}
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

/* First "SWEETXNU" in [lo, hi), scanning aligned words; 0 if none. */
static uintptr_t find_payload(uintptr_t lo, uintptr_t hi)
{
	static const uint64_t magic = 0x554e585445455753ULL;	/* "SWEETXNU" little endian */
	volatile const uint64_t *w = (volatile const uint64_t *)(lo & ~7UL);
	uint64_t prev = *w++;

	for (uintptr_t a = (uintptr_t)w; a + 8 <= hi; a += 8) {
		uint64_t cur = *w++;
		for (unsigned b = 0; b < 8; b++) {
			uint64_t v = b ? (prev >> (b * 8) | cur << (64 - b * 8)) : prev;
			uintptr_t at = a - 8 + b;
			if (at >= lo && v == magic)
				return at;
		}
		prev = cur;
	}
	return 0;
}

static struct payload hdr;	/* aligned copy of the payload header */

extern char _end[];

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start = 0, rd_end = 0;
	uintptr_t pl = (uintptr_t)_end;

	if (fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) &&
	    fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);
	if (!find_payload(pl, pl + 16)) {
		pl = rd_end ? find_payload(rd_start, rd_end) : 0;
		if (!pl)
			return "no SWEETXNU payload";
	}
	copy_any((uintptr_t)&hdr, pl, sizeof(hdr));
	const struct payload *p = &hdr;
	con_hex2("PAYLOAD", "at=", pl, "off=", pl - rd_start);
	if (p->version != 1 || p->nsegs > MAX_SEGS)
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex2("PHYS", "base=", p->phys_base >> 12, "size=", p->mem_size >> 12);	/* in 4K pages */
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes. */
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy_any(dst, pl + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
		smc(GH_WDT_PET, 0, 0, 0);
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", (p->kernel_end + pa_of_va_base) >> 12);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy_any(adt_pa, pl + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = fb.base;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = fb.stride;
	ba->Video.v_width = fb.w;
	ba->Video.v_height = fb.h;
	ba->Video.v_depth = fb.bpp * 8;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa >> 12, "top=", top >> 12);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
