/*
 * Text console on whatever framebuffer ABL left scanning out. The format is
 * taken from the MDSS source pipe (SSPP) that points at it, so 24- and
 * 32-bit layouts both work. With the MMU off every access is Device memory:
 * stay naturally aligned (-mstrict-align, byte stores for 24 bpp).
 */
#include "fbcon.h"
#include "../../shim/font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	9

struct fb fb;
static uint32_t con_bg;
static int row;

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }

/* SSPP register offsets (SDE / DPU) */
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE0	0x24
#define SSPP_SRC_FORMAT		0x30
#define SSPP_SRC_UNPACK		0x34

void fb_detect(void)
{
	static const uint32_t pipes[] = { 0x5000, 0x25000, 0x27000, 0x29000 };

	fb.base = SPLASH_BASE;
	fb.w = PANEL_W;
	fb.h = PANEL_H;
	fb.bpp = 4;
	fb.stride = PANEL_W * 4;
	fb.pipe = -1;
	for (int i = 0; i < 4; i++) {
		uintptr_t p = MDSS_MDP_BASE + pipes[i];
		uint32_t addr = rd32(p + SSPP_SRC0_ADDR);
		uint32_t size = rd32(p + SSPP_SRC_SIZE);
		if (!addr || !size)
			continue;
		fb.pipe = i;
		fb.base = addr;
		fb.w = size & 0xffff;
		fb.h = size >> 16;
		fb.stride = rd32(p + SSPP_SRC_YSTRIDE0) & 0xffff;
		fb.format = rd32(p + SSPP_SRC_FORMAT);
		fb.unpack = rd32(p + SSPP_SRC_UNPACK);
		fb.bpp = ((fb.format >> 9) & 3) + 1;
		break;
	}
	/* sanity: anything odd falls back to the splash buffer guess */
	if (fb.w == 0 || fb.w > 4096 || fb.h == 0 || fb.h > 4096 ||
	    (fb.bpp != 3 && fb.bpp != 4) || fb.stride < fb.w * fb.bpp) {
		fb.pipe = -2;
		fb.base = SPLASH_BASE;
		fb.w = PANEL_W;
		fb.h = PANEL_H;
		fb.bpp = 4;
		fb.stride = PANEL_W * 4;
	}
}

static inline void put_px(uintptr_t a, uint32_t c)
{
	if (fb.bpp == 4) {
		*(volatile uint32_t *)a = c;
	} else {
		volatile uint8_t *p = (volatile uint8_t *)a;
		p[0] = c;
		p[1] = c >> 8;
		p[2] = c >> 16;
	}
}

void con_clear(uint32_t bg)
{
	for (uint32_t y = 0; y < fb.h; y++) {
		uintptr_t line = fb.base + y * fb.stride;
		for (uint32_t x = 0; x < fb.w; x++)
			put_px(line + x * fb.bpp, bg);
	}
	con_bg = bg;
	row = 0;
}

static void put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	uint32_t x0 = MARGIN + col * CH_W, y0 = MARGIN + row * CH_H;
	if (x0 + CH_W > fb.w || y0 + CH_H > fb.h)
		return;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			uint32_t c = (g[gy] & (0x80 >> gx)) ? fg : con_bg;
			for (int sy = 0; sy < SCALE; sy++) {
				uintptr_t p = fb.base + (y0 + gy * SCALE + sy) * fb.stride +
					      (x0 + gx * SCALE) * fb.bpp;
				for (int sx = 0; sx < SCALE; sx++)
					put_px(p + sx * fb.bpp, c);
			}
		}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void con_skip(void)
{
	row++;
}

int con_row(void)
{
	return row;
}

void con_set_row(int r)
{
	row = r;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}

/* label + decimal number, padded so a redraw overwrites the old value */
void con_dec(const char *label, uint32_t v)
{
	char buf[12];
	int i = 11;
	buf[i] = 0;
	do {
		buf[--i] = '0' + v % 10;
		v /= 10;
	} while (v && i > 0);
	while (i > 1)
		buf[--i] = ' ';
	con_str(label, buf + 1);
}
