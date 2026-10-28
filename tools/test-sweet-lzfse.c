/*
 * Host test of xnu patch 2019's decmpfs LZVN/LZFSE decoding (bsd/kern/sweet_lzfse in the kernel tree): the vendored
 * reference decoder and sweet_lzfse_chunks.h, built as they are, against data from the reference encoder.
 *
 *   cc -O1 -I XNU/bsd/kern/sweet_lzfse tools/test-sweet-lzfse.c XNU/bsd/kern/sweet_lzfse/{lzfse_decode,lzfse_decode_base,
 *      lzfse_fse,lzvn_decode_base}.c ENCODER/{lzfse_encode,lzfse_encode_base,lzvn_encode_base}.c -o test && ./test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_lzfse_chunks.h"

size_t lzfse_encode_buffer(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size, void *scratch);
size_t lzfse_encode_scratch_size(void);

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void
fill(uint8_t *p, size_t n, unsigned seed)
{
	/* compressible but not trivial: words from a small alphabet with some noise */
	for (size_t i = 0; i < n; i++) {
		seed = seed * 1103515245u + 12345u;
		p[i] = (seed >> 16) % 7 == 0 ? (uint8_t)(seed >> 8) : (uint8_t)("abcdefgh"[(i / 3) % 8]);
	}
}

/* the kernel's resource-fork path (sweet_lz_fetch) over an in-memory fork */
static int
rsrc_fetch(int lzvn, const uint8_t *fork, uint64_t usize, uint64_t offset, uint64_t want, uint8_t *dst, void *scratch)
{
	const uint64_t chunks = (usize + SWEET_LZ_CHUNK - 1) / SWEET_LZ_CHUNK;
	const uint32_t *table = (const uint32_t *)fork;
	if (table[0] != (chunks + 1) * 4) {
		return -1;
	}
	if (offset + want > usize) {
		want = usize - offset;
	}
	static uint8_t out[SWEET_LZ_CHUNK];
	struct sweet_lz_vec v[3] = {{dst, (long long)(want / 3)}, {dst + want / 3, (long long)(want / 3)},
		{dst + 2 * (want / 3), (long long)(want - 2 * (want / 3))}};
	uint64_t done = 0;
	for (uint64_t c = offset / SWEET_LZ_CHUNK; c <= (offset + want - 1) / SWEET_LZ_CHUNK; c++) {
		uint64_t at = c * SWEET_LZ_CHUNK, expect = usize - at < SWEET_LZ_CHUNK ? usize - at : SWEET_LZ_CHUNK;
		size_t got = sweet_lz_decode(lzvn, fork + table[c], table[c + 1] - table[c], out, expect, scratch);
		if (got != expect) {
			return -2;
		}
		done += sweet_lz_scatter(out, at, got, offset, want, 3, v);
	}
	return done == want ? 0 : -3;
}

/* builds a type-12 fork: table, then each 64 KiB chunk as LZFSE (or stored with 0x06 when `stored` says so) */
static uint8_t *
make_fork(const uint8_t *data, uint64_t usize, int stored_every, size_t *fork_len, void *escratch)
{
	uint64_t chunks = (usize + SWEET_LZ_CHUNK - 1) / SWEET_LZ_CHUNK;
	uint8_t *fork = malloc((chunks + 1) * 4 + usize + chunks * 1024 + 64);
	uint32_t *table = (uint32_t *)fork;
	uint32_t pos = (uint32_t)(chunks + 1) * 4;
	for (uint64_t c = 0; c < chunks; c++) {
		uint64_t n = usize - c * SWEET_LZ_CHUNK < SWEET_LZ_CHUNK ? usize - c * SWEET_LZ_CHUNK : SWEET_LZ_CHUNK;
		table[c] = pos;
		if (stored_every && c % stored_every == 0) {
			fork[pos] = (c / stored_every) % 2 ? 0xff : 0x06;
			memcpy(fork + pos + 1, data + c * SWEET_LZ_CHUNK, n);
			pos += (uint32_t)n + 1;
		} else {
			size_t e = lzfse_encode_buffer(fork + pos, n + 1024, data + c * SWEET_LZ_CHUNK, n, escratch);
			pos += (uint32_t)e;
		}
	}
	table[chunks] = pos;
	*fork_len = pos;
	return fork;
}

int
main(void)
{
	void *dscratch = malloc(lzfse_decode_scratch_size() + 1), *escratch = malloc(lzfse_encode_scratch_size() + 1);
	const uint64_t sizes[] = {1, 100, 3000, 4095, 4096, 65535, 65536, 65537, 200000, 1048576 + 17};
	for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		uint64_t usize = sizes[s];
		uint8_t *data = malloc(usize), *dst = malloc(usize), *enc = malloc(usize + 4096);
		fill(data, usize, (unsigned)s + 1);

		/* type 11: the whole stream in the xattr */
		size_t e = lzfse_encode_buffer(enc, usize + 4096, data, usize, escratch);
		CHECK(e > 0, "encode %llu", (unsigned long long)usize);
		size_t got = sweet_lz_decode(0, enc, e, dst, usize, dscratch);
		CHECK(got == usize && !memcmp(dst, data, usize), "type 11 size %llu: got %zu", (unsigned long long)usize, got);

		/* type 7: a bare LZVN stream (the payload of a bvxn block; the encoder makes those below 4096 bytes) */
		if (usize < 4096 && e > 12 && !memcmp(enc, "bvxn", 4)) {
			uint32_t payload;
			memcpy(&payload, enc + 8, 4);
			got = sweet_lz_decode(1, enc + 12, payload, dst, usize, dscratch);
			CHECK(got == usize && !memcmp(dst, data, usize), "type 7 (bare LZVN) size %llu: got %zu", (unsigned long long)usize, got);
		}

		/* stored xattr data */
		enc[0] = 0x06;
		memcpy(enc + 1, data, usize);
		got = sweet_lz_decode(1, enc, usize + 1, dst, usize, dscratch);
		CHECK(got == usize && !memcmp(dst, data, usize), "stored 0x06 size %llu", (unsigned long long)usize);

		/* type 12: resource fork, whole file and windows of it, with and without stored chunks */
		for (int stored = 0; stored <= 2; stored++) {
			size_t fl;
			uint8_t *fork = make_fork(data, usize, stored, &fl, escratch);
			const uint64_t offs[] = {0, 4096, 65536 - 100, 65536, usize / 2, usize > 16384 ? usize - 16384 : 0};
			for (unsigned o = 0; o < sizeof(offs) / sizeof(offs[0]); o++) {
				uint64_t off = offs[o];
				if (off >= usize) {
					continue;
				}
				uint64_t want = usize - off < 16384 ? usize - off : 16384;
				if (o == 0) {
					want = usize;
				}
				memset(dst, 0xaa, usize);
				int r = rsrc_fetch(0, fork, usize, off, want, dst, dscratch);
				CHECK(r == 0 && !memcmp(dst, data + off, want), "type 12 size %llu stored %d off %llu len %llu: %d",
				    (unsigned long long)usize, stored, (unsigned long long)off, (unsigned long long)want, r);
			}
			free(fork);
		}
		/* garbage is refused, not decoded */
		memset(enc, 0x37, 64);
		CHECK(sweet_lz_decode(0, enc, 64, dst, usize, dscratch) == (size_t)-1, "garbage accepted (size %llu)", (unsigned long long)usize);
		free(data);
		free(dst);
		free(enc);
	}
	printf(failures ? "%d FAILURES\n" : "all LZVN/LZFSE layout tests passed\n", failures);
	return failures != 0;
}
