# asteroids: third-party sources and credits

The Nothing Phone (3a) port follows vendor sources from the phone's kernel tree
(LineageOS 24, `kernel/nothing/sm7635`, `sm7635-devicetrees`, `sm7635-modules`).
Files that contain code or programming sequences derived from them carry an
SPDX tag and the original copyright lines; the license text is in
[`LICENSES/GPL-2.0`](LICENSES/GPL-2.0) (copied from the kernel tree's
`LICENSES/preferred/GPL-2.0`).

## Code derived from GPL-2.0 sources

| Our file | License | Derived from | Copyright |
| --- | --- | --- | --- |
| `probe/probe4.c` | GPL-2.0-only | `drivers/soc/qcom/cmd-db.c` (cmd-db layout, via xnu `sweet_rpmh.c`), `drivers/clk/qcom/gcc-volcano.c`, `clk-rcg2.c` (GCC branch / RCG / DFS registers), `drivers/pinctrl/qcom/pinctrl-volcano.c` (TLMM layout), `drivers/soc/qcom/rpmh-rsc.c` (RSC version), `include/linux/soc/qcom/geni-se.h` (GENI offsets) | The Linux Foundation; Qualcomm Innovation Center, Inc. |
| `probe/probe5.c` | GPL-2.0-only | the above, plus `rpmh-rsc.c` (RSC v3 TCS offsets, AMC request), `drivers/soc/qcom/qcom-geni-se.c` (GENI init, FIFO mode, packing), `drivers/spi/spi-geni-qcom.c` (SPI setup and commands), `sm7635-modules/noth/touchscreen/focaltech_spi_v2.c` (SPI v2 framing; `crc_kermit` ports `crckermit`) and `focaltech_core.c` (power-on and reset order) | The Linux Foundation; Qualcomm Innovation Center, Inc.; FocalTech Systems, Ltd. |
| `probe/probe7.c` | GPL-2.0-only | `drivers/spmi/spmi-pmic-arb.c` (arbiter v7 APID map, owner table, observer channel, EXT_READL), `drivers/pinctrl/qcom/pinctrl-spmi-gpio.c` (PMIC GPIO registers); structure after xnu `sweet_keys.c` | The Linux Foundation; Qualcomm Technologies, Inc.; Qualcomm Innovation Center, Inc. |

Linking these probes with the project's own files (`common.c`, `fbcon.c`, the
shim's `start.S`) makes each probe image a combined work under GPL-2.0.

## Facts taken from vendor sources (no code)

Register addresses, interrupt numbers, GPIO numbers, regulator names and
voltages, the framebuffer and memory layout used in `tools/mkadt.py --board
asteroids`, `shim/`, `probe/fbcon.c` (MDSS SSPP offsets), `probe/ufs.c` and
`asteroids/NOTES.md` come from the phone's live device tree and from
`sm7635-devicetrees` (Qualcomm volcano DTSI files, Nothing `noth/asteroids-*.dtsi`).

## Kernel code written from hardware documentation

`xnu/patches/0083-sweet-asteroids-keys.patch` (`bsd/dev/sweet_keys.c`, part of the
APSL kernel) adds the asteroids key layout to sweet's own driver. It copies no
GPL code: only register addresses, offsets and bit positions (hardware facts)
from the live device tree and from the SPMI arbiter v7 layout as documented by
Linux `drivers/spmi/spmi-pmic-arb.c` (APID map at core + 0x2000, owner table at
cnfg, observer stride 0x20), verified on the phone with `probe/probe7.c`.

## Other projects

| Project | License | Use |
| --- | --- | --- |
| Apple XNU xnu-8796.141.3 | APSL 2.0 | `xnu/patches/0082-*` modifies it (like all ios-sweet patches) |
| ios-sweet (dipa-studio) | (no license file) | the base this port extends: shim, UFS driver (`probe/ufs.c` starts from `shim/ufs.c`), tools, xnu patches 0001-0081 |
| [MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz) | (no license file) | findings only (ABL drops the v4 ramdisk, LaunchServices first-boot wait); no code copied |
| [blacktop/ipsw](https://github.com/blacktop/ipsw) | MIT | host tool: shared-cache extraction and symbolication |
| [sgan81/apfs-fuse](https://github.com/sgan81/apfs-fuse) | GPL-2.0 | host tool: reads the IPSW APFS images (`tools/mkiosroot.sh`) |
| [lzfse](https://github.com/lzfse/lzfse) | BSD-3-Clause | host tool: DeviceTree IM4P payload (`tools/adt.py`) |

Apple's IPSW contents (root filesystem, shared cache, device tree) are only
used locally under `research/` and are never committed.
