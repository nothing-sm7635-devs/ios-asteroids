/*
 * asteroids boot shim: what iBoot would do before xnu, on the Nothing
 * Phone (3a) under Gunyah (see asteroids/NOTES.md for the probe results).
 *   - find the framebuffer ABL left scanning out (MDSS SSPP)
 *   - wake the UFS device: ABL leaves it NOT READY until START STOP UNIT
 *   - load the SWEETXNU payload from the initrd and jump to xnu
 * The MMU stays off throughout, as xnu's start.s expects. If loading fails,
 * the reason stays on screen and the Gunyah watchdog keeps being petted.
 */
#include <stdint.h>
#include "../probe/common.h"
#include "../probe/fbcon.h"
#include "../probe/ufs.h"
#include "loader.h"

/* start.S jumps here for PSCI CPU_ON; xnu brings up its own secondaries. */
void secondary_main(uint64_t idx)
{
	(void)idx;
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

/* The shim takes no IRQs (DAIF masked throughout). */
void probe_irq(void)
{
}

void shim_main(uint64_t dtb)
{
	stage = "fb";
	fb_detect();
	con_clear(NAVY);
	con_str("SHIM", "asteroids xnu loader");
	status_row = con_row();
	con_skip();	/* UPTIME, WDT and pet lines if we end up in hold() */
	con_skip();
	con_skip();
	con_skip();
	con_hex2("FB", "pipe=", fb.pipe, "base=", fb.base);

	stage = "ufs";
	if (ufs_wake())
		con_str("UFS", "not ready (xnu root will fail)");
	smc(GH_WDT_PET, 0, 0, 0);

	stage = "loader";
	const char *why = loader_boot(dtb);
	con_str("LOADER", why);
	hold();
}
