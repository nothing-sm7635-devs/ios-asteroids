# asteroids: Nothing Phone (3a) port - working notes

Second target for the ios-sweet XNU port. Device: Nothing Phone (3a), model A059,
codename Asteroids (IND SKU), SM7635 "volcano" (Snapdragon 7s Gen 3).
Owner's LineageOS 24 userdebug build (`adb root` works), kernel source at
`/home/saalim/os/los24/kernel/nothing/{sm7635,sm7635-devicetrees,sm7635-modules}`.

## Handoff (2026-10-19)

- **Backups done and verified** (`asteroids/backup/`, git-ignored, 14 GB, made by
  `tools/ufs-backup.sh asteroids/backup sdb sda sdc sdd sde sdf`): whole LUNs
  sda/sdc/sdd/sde/sdf, all sdb partitions except userdata (incl. super, persist),
  sdb primary+backup GPT, live.dtb. All 17 images match device-side SHA256.
  userdata (sdb13, 241 GB) deliberately NOT backed up (user declined).
- `fastboot boot` works: stock boot_b (unchanged) boots LineageOS to boot_completed.
- **Probe 1 ran (commit 3d399a2, `asteroids/probe/build.sh`, image
  `asteroids/probe/out/probe-boot.img`)**: all tests completed, results below
  (read off the screen by the user).
- **Probe 2 ran (commit e54c295, `build.sh 2`)**: SGIs all fine (see below);
  UFS transport works but every SCSI command gets CHECK CONDITION.
- **Probe 3 ran (commit 0fd195d, `build.sh 3`)**: UFS works after START STOP UNIT
  (ACTIVE) to W-LUN 0xD0; LUN 0 = Linux sdb. Byte write to GICR_IPRIORITYR is
  stored, but then 0/10 SGI1 arrive.
- **Probe 3b ran (commit 4e88844)**: LUN 0 GPT read, matches the backup GPT exactly
  (super 0x2208 9216 MiB, metadata 0x242248 64 MiB, rawdump 0x246248 256 MiB,
  userdata 0x256248 230237 MiB; userdata fs unreadable = metadata encryption).
  SGI bursts got 0/10 for byte 0x80, word 0x80 AND word 0xa0: **the probe 3 SGI
  test is invalid** - it never set GICD_CTLR group enables (0xD0, as ABL leaves
  it; probe 2 wrote 0xD3 first). So nothing is known about byte writes; moot,
  because xnu patch 0004 writes GICR_IPRIORITYR as 32-bit 0xA0A0A0A0.
  Probe 1's 1/10 remains unexplained (it did enable GICD); probe 2 shows the
  setup XNU uses works.
- **Probe phase complete.** Next: the XNU port proper (see "Port plan" below).
- **Port step 1 (commit 1d8ac31), written, NOT built or booted yet:**
  - `asteroids/shim/`: fb from SSPP, `ufs_wake()` (SSU ACTIVE), SWEETXNU loader
    that searches the v4 initrd (vendor ramdisk comes first, so the payload is
    unaligned) and copies with aligned loads (host-tested).
  - `tools/mkadt.py --board asteroids` (sweet output byte-identical, checked).
  - `xnu/patches/0082-sweet-board-neutral-asteroids.patch` (applies on 0081 and
    with blacktop's patches; verified locally in a tree built from
    xnu-8796.141.3 + stage scripts + 0001-0081): early fb from boot_args,
    GICR stride from ADT, platform expert/CPU match asteroids, SM7150 drivers
    gated on root compatible xiaomi,sweet.
  - `asteroids/build-boot.sh KERNEL NAME [ARGS]` -> out/NAME-boot.img; default
    args `debug=0xc serial=3 cpus=1 keepsyms=1`, no rd= (no storage access).
  - Loader test: `asteroids/out/test-boot.img` (shim + testpayload.S: green
    screen = boot_args/physBase/fb correct, red = wrong).
  - **Observed 2026-10-19 ~14:56:** with the payload as the boot.img v4 ramdisk
    the shim said "no SWEETXNU payload": initrd 0xB7712000-0xB7FFE97C, the same
    as a normal Linux boot = vendor_boot ramdisk 6387099 + flashed init_boot_b
    ramdisk 2968524 + 2069 B bootconfig. **ABL ignores our boot ramdisk.**
    Fix: payload appended to the shim Image at _end, arm64 header image_size
    patched to cover it (shim/build.sh); initrd search kept as fallback.
  - **Observed ~14:58: loader test SOLID GREEN** - payload found at _end, copied
    to 0x8c0004000, boot_args physBase 0x8c0000000, 32 bpp fb, stride ok, code
    ran inside kernel RAM. Shim screen before it: SSU ACTIVE then TUR lun0 ok.
- **CI not available on this host:** no `gh` CLI here. The kernel must be built by
  the xnu workflow (push of xnu/patches to main triggers it). Note: CI silently
  SKIPS patches that fail `git apply --check` - verify a patch applies first.
- Not on this host: research/ (d22ap.adt for /product), IPSW, iOS root images,
  sweet kernel builds.

## Port plan (proposed, not started)

1. Shim for asteroids: boot.img v4, framebuffer from SSPP, move the kernel out
   of ABL's 0xA8000000 into a clean RAM range (largest: 0x8bf800000..0x9f4580000
   minus reserved, ~4.8 GB contiguous; check /proc/iomem holes), generated ADT.
2. XNU: the SWEET machine config hardcodes sweet values (GICD/GICR bases, GICR
   stride 0x20000 -> 0x40000, timer INTID 19 -> 27, UFS/fb addresses, memory
   layout). Make them ADT-driven or add an ASTEROIDS config.
3. XNU UFS driver: add SSU ACTIVE to W-LUN 0xD0 before first I/O; root LUN 0.
4. Then the sweet bring-up order: fbcon, CPUs, UFS root, launchd, display, touch
   (FocalTech SPI, new), buttons (PMK8550 PON), USB (DWC3 + new PHY).

## Probe 3 results (observed, photo 2026-10-19 14:35)

| Item | Value |
| --- | --- |
| Sense before wake | TUR LUN0/LUN1: key 2 ASC 00 ASCQ 00 (NOT READY); TUR W-LUN 0xD0: 02/04/02 (not ready, initializing command required) |
| SSU ACTIVE to 0xD0 | rc 0; afterwards TUR LUN0 and LUN1 rc 0 |
| LUN last LBAs (4K) | LUN0 0x03A8BFFF (= sdb 251456913408 B, photo font shows 8 like B), LUN1 0x13FF, LUN2 0x13FF (20 MiB: sda/sdc), LUN3 0x1FFF (32 MiB sdd), LUN4 0xFFFFF (4 GiB sde), LUN5 0x1FFF (32 MiB sdf) |
| READ LUN1 LBA 0-1 | rc 0, MBR AA55 type EE, GPT "EFI PART", 32 entries at LBA 2 (all < 64 MiB, none listed) |
| GICR byte write | IPRIORITYR word A0A0A0A0 -> byte 0x80 at +1 -> reads A0A080A0, then SGI1 x10 unmasked: **0 arrived**, RPR FF |

So ABL leaves the UFS device in a low-power state; XNU's UFS driver must send SSU
ACTIVE first. Linux sdX letters do not follow LUN numbers.

## Probe 2 results (observed, photo 2026-10-19 14:31)

| Item | Value |
| --- | --- |
| SGI A (10 self-SGIs sent masked) | `3333333333`: each pending in GICR_ISPENDR0, HPPI = 1, taken once on unmask; total 10, RPR 0xFF after |
| SGI B (probe 1's unmasked case) | `123456789A` = 10/10 |
| SGI C (IDs 0-15 once) | got 0xFFFF |
| SGI D cross-CPU | CPU1 on ret 0, CPU0->CPU1 10/10 (last1 = 2); CPU1->CPU0 0xB = 10 + 1 left from test C (SGI 3) |
| GICR priorities | written as 32-bit words 0xa0a0a0a0, read back A0A0A0A0 |
| GICR_ISPENDR0 | bit 27 (vtimer) pending even though we never enabled the vtimer in probe 2 (left by ABL/Gunyah) |
| UFS regs | VER 0x300, CAP 0x1587031F (32 slots), HCE 1, HCS 0x10F (DP, UTRLRDY, UTMRLRDY, UCRDY; UPMCRS 1), IS 0x400 (UPMS), UTRLBA was 0xD2F2D000 (ABL's), DBR 0 |
| UFS takeover | our list at 0xA8006000 accepted (RSR 1) |
| UFS commands | TUR LUN0 x3: rc 4 (OCS 0 = transport ok, SCSI status 0x02 CHECK CONDITION); READ CAPACITY LUN0-5 all fail; READ LUN1 LBA0 rc 4 |

Interpretation (guess): DMA through the SMMU works (UTRD/UCD read, response UPIU
written). Probe 1's 1/10 SGI result differed from probe 2 in using byte writes to
GICR_IPRIORITYR (and leaving vtimer enabled): Gunyah's vGIC may ignore byte
writes. Not verified yet.

## Probe 1 results (observed)

| Item | Value |
| --- | --- |
| Entry | EL1, MPIDR 0x81000000 (boot CPU = Cortex-A520, MIDR 0x410FD801), Image loaded at 0xA8000000, DTB at 0xB7511000 (magic ok) |
| Framebuffer | SSPP pipe 0 (VIG0, 0xae05000) SRC0_ADDR 0xE3940000, 1080x2392, stride 4320, 32 bpp, SRC_FORMAT 0x237ff, UNPACK 0x03020001; 0x00RRGGBB colours show correctly |
| Gunyah vWDT | STATUS (0x86000006) ctl=0 -> disabled; "since last pet" counts; PET returns ok. Phone stayed up indefinitely |
| CPU | ID_AA64MMFR0 TGran16=1 (16K ok), PARange=2 (40 bit); PFR0 GIC=3 (sysreg iface), EL2=1 |
| PSCI | v1.1, SMCCC 1.3; CPU_ON ret 0 for CPU1-7, MPIDRs 0x81000100..0x81000700 |
| MMU | 16K, T0SZ=28, IPS 40 bit, 3 L3 tables; PAR img ok (WB); 64K-word loop 17060 us -> 329 us |
| GICD 0x17100000 | CTLR 0xD0 before, 0xD3 after writing |=0x13; TYPER 0x016B001F; IIDR 0x47000070 (not ARM's 0x43B: probably Gunyah's vGIC); PIDR2 0x30 (GICv3) |
| GICR | frame for the boot CPU at 0x17180000 (stride 0x40000), WAKER 0 |
| ICC | SRE 1 already; CTLR 0x88400; IGRPEN1 1 |
| vtimer | INTID 27, 30 IRQs in 300 ms (10 ms period) = correct |
| SGI 1 to self | **only 1 of 10 arrived**: the first (sent masked, pending, HPPI=1) was taken; the 9 sent later with IRQs unmasked did not arrive. Same code gave 10/10 on sweet. Cause unknown |

## Hardware facts (from live DT / kernel source)

- CPUs: 4x Cortex-A520 (0xd80) + 4x Cortex-A720 (0xd81), cpu@0..cpu@700, PSCI via smc. Features incl. PAC/BTI.
- Gunyah hypervisor (`qcom,gunyah-hypervisor`), HLOS runs as a VM at EL1.
- GICv3: GICD 0x17100000, GICR 0x17180000 (8 x 0x40000). Timer PPIs: sec 13, phys 14 (INTID 30), virt 11 (INTID 27), hyp 12. 19.2 MHz.
- RAM: many System RAM ranges 0x80e00000..0xffffffff and 0x880000000..0x9ffffffff (see /proc/iomem).
- Display: MDSS mdp_phys 0x0ae00000, SSPP offsets 0x5000/0x25000/0x27000/0x29000; panel RM69220 1080x2392 video mode; splash_region 0xe3940000 size 0x2b00000.
- Storage: UFS ufshc@1d84000; userdata = sdb13 (4K sectors).
- Touch: FocalTech `fts_ts` on SPI (qupv3_0 0xa80000 spi0). Buttons: PMK8550 PON (pwrkey/resin) over SPMI 0xc42d000, plus gpio-keys.
- USB: ssusb@a600000 (DWC3).
- No IMEM restart_reason node (reboot reason lives elsewhere).
- Boot image: header v4, no ramdisk in boot; DTB + vendor ramdisk from vendor_boot. Stock kernel gzip; our raw Image is accepted.
- Toolchain on this host: no aarch64 GNU gcc; use LOS `prebuilts/clang/host/linux-x86/clang-r596125` + `system/tools/mkbootimg`.
