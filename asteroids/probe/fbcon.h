#pragma once
#include <stdint.h>

#define MDSS_MDP_BASE	0x0ae00000UL	/* mdp_phys; SSPP offsets are relative to it */
#define SPLASH_BASE	0xe3940000UL	/* reserved-memory splash_region (0x2b00000) */
#define PANEL_W		1080
#define PANEL_H		2392

/* colors are 0x00RRGGBB; on a BGR layout red/blue swap, which is harmless */
#define BLACK	0x00000000U
#define NAVY	0x00000060U
#define BLUE	0x000000a0U
#define WHITE	0x00ffffffU
#define YELLOW	0x00ffff00U

struct fb {
	uintptr_t base;
	uint32_t w, h, stride, bpp;
	uint32_t format, unpack;
	int pipe;	/* SSPP index it came from; -1 none found, -2 bogus values */
};

extern struct fb fb;

void fb_detect(void);
void con_clear(uint32_t bg);
void con_skip(void);
int con_row(void);
void con_set_row(int r);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
void con_dec(const char *label, uint32_t v);
