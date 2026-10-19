# asteroids: Nothing Phone (3a) port - working notes

Second target for the ios-sweet XNU port. Device: Nothing Phone (3a), model A059,
codename Asteroids (IND SKU), SM7635 "volcano" (Snapdragon 7s Gen 3).
Owner's LineageOS 24 userdebug build (`adb root` works), kernel source at
`/home/saalim/os/los24/kernel/nothing/{sm7635,sm7635-devicetrees,sm7635-modules}`.

## >>> 2026-10-19 ~10:52 UTC: iOS 16.7 LOCK SCREEN ON THE NOTHING PHONE (3a) <<<

Boot root3 (`asteroids/out/root3-boot.img`, started 10:37:52 UTC; kernel = CI
kernel.development.sweet with 0082, ADT --board asteroids with d22ap /product,
userdata = image a1 after two interrupted boots, cpus=8, sweetfb_noconsole=1,
sweetsample=SpringBoard,lsd,backboardd) left undisturbed: the user reported the
lock screen roughly 6-15 min after start (exact time unknown; reported at ~10:52).
So the root2 hang was the first-boot LaunchServices reseed (as on ios-topaz),
just slow; it completed this time. No touch / buttons yet (sweet drivers gated
off): next = FocalTech touch on QUP SE0 SPI (GPIO 19 irq, 20 reset, L22B/L11B),
PMK8550 PON keys.

**Touch bring-up, probe 4 (read-only), 2026-10-19 16:30:** first run used TLMM base
0xf000000 - WRONG: pinctrl-volcano.c adds REG_BASE 0x100000, GPIO n = 0xf100000 +
n*0x1000. The read at 0xf013000 stalled the CPU and the phone reset (user held
vol-: back to fastboot, Android not booted). Observed before that: GCC vote
0x52008 = 0x48000900 (QUP0 bits 18-22 clear), SE0 CBCR 0x18004 = 0x80004000 (off),
0x23000/0x23004 read 0, core 1x/2x 0x00004000 (on); SE0 RCG cmd 0x80000000 cfg
0x00100000, **CMD_DFSR 0x21 (DFS on)**, PERF_DFSR l0 0x1 (TCXO 19.2 MHz), l1-l4
0x2601 (GPLL0 even), l5 0x10b, l6 0x109 (GPLL0 main), l7 0.
Probe 4 fixed (16:34): GPIO0/1 cfg 0x203 io 3 (GPIO out high), GPIO2/3/19/20
cfg 0x1 io 0 (in, pull-down: touch held in reset); RSC DRV2 id 0x00030200 =
**v3.2** (v3 TCS offsets), solver 0x04010100, prnt 0x800C8104 (8 TCS, 16 cmds);
cmd-db ok: ldob22 = 0x42800, ldob11 = 0x40A00. GENI not read (clocks off).
**Probe 5 (16:38, commit 9148c4f):** pins muxed (g0 0x84), RPMh v3 AMC votes
ldob11 1800 mV + ldob22 3300 mV **completed** (cmd status 0x10101), GCC QUP0
votes (0x487C0900), SE0 clock on, reset released (g20io 3, g19io 1). SE0 fw
0x102 **proto 1 = SPI**, HW_PARAM_0 0x202029E4 (FIFO depth 32), **but
GENI_IF_DISABLE_RO = 1: FIFO interface disabled -> SE0 only works through GPI
DMA** (spi-geni-qcom.c falls back to GPI when fifo_disable). Chip-ID reads in
FIFO mode: all zero, M_IRQ 0x2, no CMD_DONE. Next: minimal GPI DMA path.
GPI node (live DT): qcom,gpi-dma@a00000 size 0x60000, gpi-ee-offset 0x10000,
max-num-gpii 12, gpii-mask 0x3e (HLOS 1-5), static-gpii-mask 0x1, iommus SID
0x576, iommu-dma-addr-pool 0x100000+0x100000, dma-coherent; SE0 dmas =
<gpi 0 0 1 64 2>, <gpi 1 0 1 64 2>.

**Buttons, probe 7 (16:54, commit 53ad0a2), all four work, read-only:** SPMI arbiter
v7 (core 0xc400000 ver 0x70010000, 0x380 APIDs), APID map core+0x2000+4n, owner
cnfg(0xc42d000)+4n, observer 0xc440000 + 0x20*apid, EXT_READL (1<<27)|(addr&0xff)<<4.
PON (PPID 0x013) = APID 3 owner 0; vol-up GPIO (PPID 0x18d) = APID 0x1F owner 0.
PON RT_STS 0x1310 idle 0x10 (bit 4: probably USB/charger), +bit 7 power (0x90),
+bit 6 vol- (0x50). PMXR2230 GPIO6 0x8d05 subtype 0x13, 0x8d40 mode 0 (input),
0x8d46 en 0x80, 0x8d10 RT bit 0 = 1 idle, 0 pressed. AI key: TLMM GPIO 71 io bit 0
active low. 0 read errors.
**xnu 0083** (`xnu/patches/0083-sweet-asteroids-keys.patch`, applies on 0082 + blacktop,
NOT built yet): sweet_keys.c takes a per-board table (sweet v5 unchanged; asteroids
v7 values above, AI key -> key 3 Home/Menu); sweet_ufs.c now starts the keys on
every board (driver refuses unknown boards), touch/USB stay sweet-only. Test: CI
kernel -> `ASTEROIDS_ALLOW_UFS_WRITES=yes asteroids/build-boot.sh KERNEL keys1
<root3 args>` -> boot, log should show "SWEET KEYS: nothing,asteroids arb=0x70010000".

**Probe 8 (2026-10-19 17:10, written + built, NOT run yet):** `asteroids/probe/build.sh 8`
-> `probe/out/probe8-boot.img` = probe 5's bring-up (pins, RPMh rails, GCC, reset)
with SE0 in GSI mode + a polled GPI DMA driver (`probe/gpi.c`): static GPII 0
(regs at 0xa00000 - ee-offset 0x10000 + 0x20000...), ch 0 TX / ch 1 RX, SE id 0,
proto SPI, 16-entry rings in the image (MMU off, PA = VA), config0 (div 20 from
19.2 MHz, ~1 MHz) + GO full duplex + DMA TREs as spi-msm-geni.c. Then chip ID
0xA3/0x9F/0xA6 and a loop that reads the 84-byte touch report (reg 0x01) on IRQ-low
or every 50 ms and draws a dot per finger (yellow id 0). Screen keys: "GPII0"
pre-state, "GPI up", "ID ..." lines (`mirq=` now = nev<<16|tx code<<8|rx code;
tx 02 = EOT, rx ff = RX TRE consumed without event), "TOUCH reads/errs",
" pts p0=" IFXXXYYY (id, flag, x, y hex). Assumptions to check on the photo
(guesses): apps SMMU passes SID 0x576 untranslated as for UFS; QUP0->DDR path
works without an interconnect vote. Read-only for storage; vol- + power exits.
Run: phone in fastboot, `fastboot boot asteroids/probe/out/probe8-boot.img`.
**Probe 8 run 1 (17:13, photo):** bring-up as probe 5 (rails ok, s0 clock on,
reset released), gsi dma=1 ev=0xF, GPII0 all 0 before (not allocated, type 0),
**"GPI up" ch0=0x1020000A (STARTED, el 16, out, GPI) ev=0x10110002 (ALLOCATED)**,
then the screen froze before the first "ID" line, no reset. Cause unknown; likely
my event loop: it spun forever if the HW event RP is outside the ring (no bound
check) - or a stalled register read. Fixed (17:14): RP sanity check (badrp=),
"GPI up" also shows ch1/evrp/ring addresses, a one-time "GPI db" line after the
first doorbell, gpi_dump when the chip ID is not 0x56.
**Probe 8 run 2 (17:15, photo):** "GPI up" ch0=0x1020000A ch1=0x10200102 (both
STARTED), ev=0x10110002, evrp=0xA8004E00 = ring base (ring 0xA8004E00, tx ring
0xA8005100), then **the phone reset before the "GPI db" line**, i.e. at the
doorbell writes (or the RP read right after). Guess: the GPI's first DMA (TRE
fetch, SID 0x576) faults at the apps SMMU or on the QUP0->DDR NoC path and
that is fatal. Run 3 build (17:16): read-only apps SMMU dump after "GPI up"
(sCR0, ID0, sGFSR, SMR/S2CR matching SIDs 0x576 GPI / 0x60 UFS / 0x1c00 MDP),
then a 15 s countdown before the doorbell.
**Probe 8 run 3 (17:17, photo), apps SMMU (observed):** sCR0 0x002B0406
(USFCFG=1: unmatched streams FAULT; GFRE/GFIE set; CLIENTPD 0; EXIDENABLE 0),
ID0 0x4C017E72 (114 SMRs), sGFSR 0. **SID 0x576 (GPI): no SMR.** SID 0x60 (UFS):
SMR 0 = 0x80000060, S2CR 0 = 0x00000000 (translate via context bank 0 - which
passed our PA DMA in probe 2, so CB0 maps it 1:1 or S1-bypasses). SID 0x1c00
(MDP): SMR 2 = 0x80021C00, S2CR 0x2 (CB 2). Reset again at the doorbell.
Conclusion: GPI DMA faults as an unmatched stream; the fault resets the SoC.
Run 4 build (17:19): `gpi_smmu_route()` copies UFS's S2CR into the highest free
SMR slot and sets SMR = valid | 0x576 (runtime only), stops if not read back,
5 s countdown, doorbell. XNU will need the same SMR (or its own CB) for touch.
**Probe 8 run 4 (17:20, photo): PANIC SYNC in stage smmu**, ESR 0x96000010 (data
abort, synchronous external abort, read), FAR 0x15000800 (SMR 0), ELR 0xA8002B98 =
`ldr w12, [x10], #4` in gpi_smmu_route's scan loop (post-indexed). The same SMR
reads worked in gpi_smmu_check with plain `ldr`. Inference (strong, not verified
otherwise): **Gunyah traps and emulates the apps SMMU for HLOS** and cannot emulate
accesses without a valid ISV syndrome (writeback / pair forms) -> injects an
external abort. Fix (17:22): common.h rd32/wr32/rd64/wr8 are inline-asm plain
ldr/str. **XNU consequence:** any trapped MMIO (SMMU, maybe others) must use plain
single-register accessors; check sweet drivers' MMIO helpers before touching the
SMMU from XNU.
**Probe 8 run 5 (17:23, photo):** no abort; ROUTE slot 0x71 s2cr 0 (copied from
UFS) smr 0x80000576 read back, gfsr 0; countdown ran, **reset again at the
doorbell**. So the SMR (as far as Gunyah's emulated view shows) is not enough.
Next suspect (guess): NoC. qhm_qup0 is on aggre2_noc -> qnm_aggre2_noc = BCM
**SN3** (volcano.c); UFS is on aggre1 (SN2), which the boot chain uses. Run 6
build (17:26): `bcm_on("SN3")` = one AMC command 0x60004001 (commit, valid,
x=1, y=1) via cmd-db slave 5, before the countdown; the SMMU dump lines were
dropped (results above) to keep the screen in one page.
**Probe 8 run 6 (17:28, photo):** SN3 cmd-db addr 0x5001C, AMC ok (s0 0x10101);
ROUTE as run 5; **reset again at the doorbell**. anoc_2_qtb (TBU for SIDs
0x400-0x7ff, incl. GPI 0x576; UFS 0x60 is on anoc_1_qtb) votes system_noc
MASTER_A2NOC_SNOC = SN3 too, so that is covered; no TBU GDSC in the DT.
Run 7 build (17:30): `gpi_stage_test()` splits the doorbell, 3 s countdown
before each step: "DB0" = TX doorbell with no new TRE; "NOP" = one NOOP TRE with
IEOT, then only the event ring in memory is polled (1 s) before any GPI read;
then "SPI" = the chip-ID transfer. The last label on screen before the reset
tells which step kills it. Ask the user for a video.
**Probe 8 run 7 (17:31, photo): NO RESET.** DB0 (doorbell, no TRE) fine. NOP TRE
(IEOT): **ch0 RP 0xA8005C00 -> 0xA8005C10 = GPI fetched the TRE from DDR through
the routed SMMU stream** (route + SN3 vote), but no event in memory within 1 s
(d3 0, ev RP unchanged), ch0 still STARTED, type/glob/gpii/errlog all 0. Open:
GPI DMA *writes* (events, RX data) vs SE0 activity as the reset trigger.
Run 8 build (17:32): screen cleared, steps NOP(IEOB), LOCK, UNLOCK (IEOB: should
each write an event), then TX-only SPI (config0 + GO TX_ONLY + TX DMA IEOT, no RX),
each after a 3 s countdown with memory-only polling; then the full-duplex reads.
**Probe 8 run 8 (17:34, video):** NOP(IEOB), LOCK, UNLOCK each completed with an
event in memory: d2 0x04000000 (code 4 EOB), d3 0x00220000 (type 0x22, ch 0), 16-18
us, ch0 RP 0xD10/0xD20/0xD30. **So GPI TRE reads and event writes to DDR work.**
**Reset at "SPI tx"** (config0 + GO TX_ONLY + TX DMA): froze right after "go",
CPU only polling memory -> a hardware-side fatal error once SE0 runs a frame.
Guess: another master/SID (TX data fetch, or SE0/QUP wrapper SID 0x563) faulting
at the SMMU -> bus error -> TZ reset. Run 9 build (17:36): after routing, sCR0
GFRE|GFIE cleared (global faults RAZ/WI, no abort/interrupt), sCR0 printed after
the screen clear, LOCK/UNLOCK kept as event sanity, and after "SPI tx" sGFSR +
sGFSYNR0/1/2 (syn1 = faulting SID) are printed.
**Probe 8 run 9 (17:37, photo): NO RESET, frames complete.** sCR0 now 0x002B0400
(write took). SPI tx: event d2 0x0200000A (EOT, 10 bytes) d3 0x00220003, 206 us;
sGFSR/syn0-2 all 0. Full-duplex chip-ID reads: gpi_status 0x000N0202 = EOT on
TX and RX; **but all RX bytes 0** -> CRC fail (-3) on every read; touch loop
0x29E reads, all errors; **IRQ GPIO 19 seen low 0x137 times** (chip firmware
alive, guess). Interpretation (guess): a fault still happens, now silent RAZ/WI
(clearing GFRE/GFIE is what stopped the reset), though sGFSR stays 0.
Run 10 build (17:38): rx_buf canary 0xA5; route SID 0x563 (QUP0 wrapper / SE
DMA) to CB0 as well; "LOOP" step = 16-byte frame with config0 loopback flag
(GSI_LOOPBACK_EN): same=0x10 means GPI moves data both ways, A5 = RX write
dropped, 00 = TX read zero / no RX.
**Probe 8 run 10 (17:40, photo + user report): TOUCH WORKS.** Loopback rx
01122334 45566778, same=0x10 (DMA both ways). Chip ID reads with CRC ok:
**0xA3/0x9F = 0x56/0x72 = FocalTech FT3683G, FW 0xA6 = 0x41.** Touch loop 0xBD reads,
0 errors, report FF.. when idle; user: dots appear wherever the screen is touched
(coordinates map 1:1 to the 1080x2392 panel). sGFSR still 0 throughout.
**The difference from run 9 was routing SID 0x563** (QUP0 wrapper, live DT
qupv3_0_geni_se iommus) to CB0: the SE/QUP data path masters with 0x563, the
GPI ring/event traffic with 0x576. Commit with these results: see git log.

**Recipe for XNU touch on asteroids (all verified by probe 8, run 10):**
1. TLMM GPIO 0-3 func 1 6 mA, GPIO 20 reset out low, GPIO 19 in pull-up.
2. RPMh AMC: ldob11 1800 mV, ldob22 3300 mV (VRM), BCM SN3 0x60004001.
3. GCC 0x52008 |= 0x1f << 18, wait SE0 CBCR on; 2 ms, reset high, 200 ms.
4. apps SMMU (Gunyah-emulated; plain single ldr/str only!): free SMR slots ->
   S2CR copied from UFS's (SMR 0, S2CR 0 = CB0), SMR = valid | 0x576 and
   valid | 0x563. sCR0 GFRE|GFIE were also cleared in this run; untested whether
   still needed once 0x563 is routed (it stopped the reset in run 9).
5. SE0 GSI mode (geni_se_select_gpi_mode), GPII 0 init as gpi.c, config0 (div 20
   on DFS level 0) + GO full duplex + TX/RX DMA TREs; FocalTech SPI v2 framing,
   touch report = 84 bytes at reg 0x01 (protocol v2 layout).
Open: interrupts (GPI GIC SPI 0x1b1, touch GPIO 19 falling edge) instead of
polling; test without the sCR0 change.

**2026-10-19: user reports TOUCH WORKS in iOS with the 0084 kernel** (booted with
the boot-args from the 0084 test plan; build/run ID, exact args and log not recorded
in this session - add them when known). Still open: drop sCR0 change
(`sweettouchgf=1` test), interrupts instead of polling.

**xnu 0084 (2026-10-19 ~17:55, written, NOT built, NOT booted):**
`xnu/patches/0084-sweet-asteroids-touch.patch` applies on 0001-0083 (checked in a
clean `tools/xnu-tree.sh` worktree, files MATCH). New `bsd/dev/sweet_touch_asteroids.c`
= probe 8 run 10 sequence in a kernel thread (starts `sweettouchdelay` s after UFS
init, default 45): TLMM, RPMh v3 AMC ldob11/ldob22/BCM SN3, GCC, reset, SMMU
SMR for 0x576 + 0x563 -> UFS's S2CR, sCR0 GFRE|GFIE cleared (boot-arg
`sweettouchgf=1` keeps them: the test of whether it is still needed), SE0 GSI,
GPII 0, FocalTech chip ID, then polls the report (IRQ GPIO 19 low / while
touched / every 100 ms, 10 ms tick) -> `sweet_hid_touch()`. Differences from the
probe (untested): DMA page from kmem_alloc_contig above 4 GiB (TRE/ring MSBs
programmed), cache clean/invalidate around every frame, any frame timeout stops
the driver (prints GPI state). Boot-args: `sweettouch=0`, `sweettouchdiv=N`
(SPI = 19.2 MHz / N, default 20). SweetHID.cpp: SweetHIDTouch takes the panel
size from the boot framebuffer (1080x2392 here; sweet still 1080x2400).
sweet_ufs.c calls `sweet_touch_asteroids_init()` on non-sweet boards.
Syntax-checked locally only with stub headers (clang -Weverything minus XNU's
-Wno list: clean apart from C++-compat notes that sweet drivers share). No `gh`
on this host: CI needs a push of xnu/patches to main (user's call).
Test plan: CI kernel -> `ASTEROIDS_ALLOW_UFS_WRITES=yes asteroids/build-boot.sh
KERNEL touch1 <root3 args>` -> boot to lock screen; log lines "SWEET TOUCH:";
expect "FocalTech id=56:72 fw=0x41" then "SWEET HID: touch 0 down at x,y".

## >>> 2026-10-19 ~18:50 local: TOUCH + BUTTONS WORK IN iOS <<<

Kernel: CI build with 0083 (keys) + 0084 (touch over GPI DMA), sha256 76227c04...b43fb
(`xnu/out/kernel.development.sweet`). Boot image `asteroids/out/touch1-boot.img`
(root3 args), booted 13:15:42 UTC. User report (observed): lock screen, power /
volume / AI(Home) keys work, touchscreen works (unlock, navigation, Settings).
Known issue: Settings > General > About does not open. Likely the sweet issue
(NOTES.md: Preferences SIGTRAP in PodcastsUI, force-unwrap of
URL.volumeTotalCapacity, nil on the HFS+ root) - unverified on asteroids; needs
the kernel log (recovery + tools/sweet-log-pull.sh) to confirm.
Log of touch1 (`logs/asteroids-touch1.txt`, 272358 B): "SWEET KEYS: nothing,asteroids
arb=0x70010000 apids=896 pon-apid=3 gpio-apid=31 ... home=1", "SWEET TOUCH: ...
starting in 45 s", RPMh ldob11/ldob22/SN3 done. **No CRASH Preferences** (crashes:
mediaserverd x2 sig 6, PosterBoard sig 5, driverkitd); Preferences exec'd twice, no
exit reason logged -> About is probably a hang, not the sweet crash. touch2 (13:22:38
UTC) = touch1 + `sweetsample=Preferences,SpringBoard` to sample Settings' threads.
**touch2 result (`logs/asteroids-touch2.txt`):** Preferences main thread (2 samples):
`-[PSGAboutDataSource _macAddress]` -> `_SCNetworkInterfaceCopyAllWithPreferences` ->
dispatch_once wait; t1 holds it in `__wait_for_IOKit_to_quiesce` ->
`SCDynamicStoreNotifyWait` (also blocks `hasEthernetNetworkInterfaces`). configd
source (`research/configd-1241`, tag configd-1241.140.6): SCNetworkInterface.c:4894
waits for Plugin:InterfaceNamer to hold Quiet / Complete (iOS) / Timeout.
Plugins/InterfaceNamer/ifnamer.c setup_IOKit(): busy-interest notification on the
IOService root + IOServiceGetBusyState, then a 300 s timer (WaitStackTimeout) that
writes Timeout; any earlier failure returns without a timer (logs only to os_log).
So: IOKit never quiet AND (setup failed or plugin thread stuck). touch3 (13:29:43
UTC) samples configd + Preferences to see which.
**touch3 result (`logs/asteroids-touch3.txt`):** About opened after ~6 min (user). configd
idle in all 3 samples (every thread in its CFRunLoop): InterfaceNamer set up fine and
its 300 s WaitStackTimeout timer released Settings. **Root cause: the IOKit registry
never goes quiet on asteroids** (some service stays busy). **xnu 0085**
(`0085-sweet-busy-services.patch`, applies on 0084 + blacktop, NOT built): SweetPlatformExpert
logs "SWEET BUSY: <name> (<class>) busy=N provider=..." for every busy IOService
sweetbusy=N s (default 120, 0 off) and 2N s after start. Workaround if needed:
WaitStackTimeout (seconds) in the InterfaceNamer bundle Info.plist of the root image
(ifnamer.c exec_InterfaceNamer reads it).
**busy1 (CI kernel with 0085, sha256 d10fc85a...ac096, booted 15:09:44 UTC,
`logs/asteroids-busy1.txt`): "SWEET BUSY: round 1/2, root busy=1, 0 busy services"** -
no driver is busy; the root holds its own count. Cause: StartIOKitMatching()
(iokit/Kernel/IOStartIOKit.cpp) adds root->adjustBusy(1) when
OSKext::iokitDaemonAvailable() (true unless -restore), dropped only by
IOService::iokitDaemonLaunched() when kernelmanagerd/kextd reports in. The iOS
16.7 root has no kernelmanagerd/kextd (OS dmg checked, launchd.plist has no job),
so the registry is never quiet. Affects sweet too (same macOS-config kernel).
**xnu 0086** (`0086-sweet-no-iokit-daemon-busy.patch`, applies on 0085 + blacktop,
NOT built): skip that busy count on ARM64_BOARD_CONFIG_SWEET only (forcing
iokitDaemonAvailable() false would make kext requests panic, OSKext.cpp:10202).
Expect: About opens at once; "SWEET BUSY: root busy=0".

**Since 2026-10-19 20:52 a second phone (serial b4479a49) is on this PC's USB. Always
address the Nothing Phone (3a) explicitly: `fastboot -s 00163352L002594 ...`,
`adb -s 00163352L002594 ...`; never send anything to the other device.**

## Open: no wallpaper/widgets, sluggish (user report 2026-10-19 evening)

**PosterBoard** (draws iOS 16 wallpapers) crashes once on every full boot (touch1-3,
busy1; sig 5): BSAssert "Invalid condition not satisfying:
!_stateLock_switcherConfiguration" (PBFPosterExtensionDataStore.m) in
`-[PBFPosterExtensionDataStore _stateLock_setupSwitcherConfigurationForFirstTime]`,
called from `_stateLock_updateExtions:...` when the in-memory ivar byte self+0xe9
(first setup done) is 0, i.e. a switcher configuration already exists (loaded from
disk?) but the first-setup path still runs. From prewarmWithCompletion (PRSServer XPC).
Not seen on sweet notes. Next: find where the configuration is loaded and why the
flag is not set (RE of PosterBoard.framework in research/ipsw-x/dyld).
**Sluggishness** candidates: (1) 300 s IOKit-quiet stalls (fixed by xnu 0086, not
built yet); (2) CPU clocks never raised after ABL (topaz needed a cpufreq patch) ->
probe 9 measures PMU cycles vs timer on CPU0 (A520) and CPU4 (A720); (3)
bluetoothd/fairplayd relaunch loops.
**Probe 9 (20:57, observed): CPU clocks are fine** - PMU cycles vs timer over 50 ms:
CPU0 Cortex-A520 (MIDR 0x410FD801) 1593 MHz x3; CPU4 Cortex-A720 (0x410FD811)
2351 / 2400 / 2400 MHz; PMCR 0xA041, PMU usable at EL1 under Gunyah. So (2) is
ruled out at handoff (not yet measured while iOS runs).
**fix1 (CI kernel with 0086, sha256 5652d5db...19ca7, 15:41 UTC, `logs/asteroids-fix1.txt`):
"SWEET BUSY: root busy=0, 0 busy services" -> registry quiet (0086 works).** About: slow on
the first open, instant afterwards (user). PosterBoard still asserts once.
PosterBoard RE (`ipsw dyld disass --image PosterBoard`): ivar self+0x98 = switcher
configuration, written only by setupSwitcherConfigurationForFirstTime and by the lazy getter
`_stateLock_switcherConfiguration` (-> `_stateLock_buildSwitcherConfigurationWithContext:`);
flag self+0xe9 set only in `_stateLock_updateExtensions:...` right after the first-time setup.
Many callers use the getter (switcherConfiguration, _stateLock_activeConfiguration, snapshot
fetches, ...): if any runs before the first extension update, the config exists and the
setup asserts. Hypothesis (unverified): poster extensions are not known when the data store
starts here, so its first update comes late. **assetsd** (Photos) aborts on every launch
(5/5): uncaught ObjC exception in a dispatch block (objc_terminate -> abort), reason not
logged. fix2 (15:51 UTC) = fix1 + `sweetscan=assetsd,PosterBoard sweetbusy=0` to print
heap strings (exception reason) at the crash.
**fix2 (`logs/asteroids-fix2.txt`), heap strings at the PosterBoard crashes:** "Switcher
configuration yielded no home or lock screen poster paths" (no posters at all), the
first-setup BSAssert, then a 2nd instance: NSInvalidArgumentException "-[__NSPlaceholderDictionary
initWithObjects:forKeys:count:]: attempt to insert nil object from objects[1]". assetsd did not
run this boot. **Root cause direction: PosterBoard finds no poster providers.** They are in
the image (extension point com.apple.posterkit.provider): WallpaperKit CollectionsPoster.appex,
PhotosUIPrivate PhotosPosterProvider.appex, EmojiPoster/GradientPoster/UnityPoster/PridePoster
*Extension.appex, ExtragalacticPoster, WeatherPoster, PaperBoard LegacyPoster. So app-extension
discovery (ExtensionKit / pkd, LaunchServices plugin records) returns nothing - probably also why
there are no widgets. Next: get os_log of pkd/PosterBoard (OS_ACTIVITY_DT_MODE=YES on their
launchd jobs; stdout reaches the kernel log via the 0048 /dev/null tee) - needs an image a2.
**a2 built by user (asteroids/mkroot-a2.sh: a1 + tools/launchd-env.py for PosterBoard, pkd,
lsd, lsd.system, chronod, assetsd), sha256 0b8e4990...; FLASHED (user confirmed) 16:00:02-
16:05:38 UTC, 24/24 OKAY (`research/flash-userdata-a2.log`). userdata = fresh a2 (first boot
rebuilds the LS database again). Boot `asteroids/out/dbg1-boot.img` = fix1 args +
`sweetbusy=0 sweetscan=assetsd,PosterBoard sweetnull=PosterBoard,pkd,lsd,chronod,assetsd`.
**dbg1 result:** the sweetnull tee caught nothing (env verified in a2 by the user; daemons'
os_log does not reach stderr). Instead: **full unified log** - copied the iOS volume from
recovery (`dd` of the first 16 GiB of userdata -> research/asteroids-dumps/userdata-ios-dbg1.img,
read-only), user mounted it ro,force and copied /private/var/db/diagnostics + uuidtext into
`research/asteroids-dumps/dbg1.logarchive` (4 uuidtext files unreadable: dirty journal),
decoded with mandiant/macos-UnifiedLogs `unifiedlog_iterator -m log-archive` (built in the
session scratchpad) -> `research/asteroids-dumps/dbg1-log.jsonl`, 2.6 M messages.
**ROOT CAUSE (no wallpaper/widgets/extensions):** lsd: "Failed to read extension point cache
... Error Domain=com.apple.extensionKit.errorDomain Code=12 \"Unable to find AMFI\"" ->
MobileInstallation ignores every .appex ("specifies a value (com.apple.widgetkit-extension)
for the NSExtensionPointIdentifier ..."); pkd "could not create extension point record ...
-10814", "Candidate plugin count from LaunchServices: 0"; PosterBoard "extension point not
found", "Received 0 extensions"; chronod widgetkit query "0 plugins". ExtensionFoundation
(0x193735ca0): IOServiceMatching("AppleMobileFileIntegrity"), IOServiceOpen type 0,
IOConnectCallMethod selector 6 (1 scalar, struct = cdhash) == 0 -> "cdhash ... is trusted".
**xnu 0087** (`0087-sweet-amfi-service.patch`, applies on 0086 + blacktop, NOT built):
iokit/Kernel/arm/SweetAMFI.cpp = AppleMobileFileIntegrity IOService (IOResources) + user
client: selector 6 -> success, others kIOReturnUnsupported, all logged "SWEET AMFI: ...".
The LS database is built once on first boot, so after the new kernel userdata needs a fresh
image (re-flash a2) for the extensions to be registered.

## !!! DANGER while userdata holds iOS: NEVER let the phone boot Android !!!

vendor_boot's fstab.default has userdata `latemount,wait,check,formattable,...`
(f2fs + metadata encryption): Android init reformats a userdata it cannot mount
-> the iOS image is gone (recoverable only by re-flashing the .simg). ABL boots
Android whenever a reset happens without volume-down held. Always hold
power + vol- until the fastboot screen. Lineage recovery (fastboot reboot
recovery) does not mount_all, it is the way to read the log (tools/sweet-log-pull.sh).
recovery_b has no kernel (GKI: kernel from boot_b), so it cannot be `fastboot boot`ed.

## >>> UPDATE 2026-10-19 15:13: FIRST XNU BOOT ON ASTEROIDS WORKS <<<

CI kernel (run on danascape/ios-android, log: "Applying patch ... 0082", built
09:38 UTC, kernel.development.sweet sha256 4017e59b...3491390, in xnu/out/,
git-ignored) + `asteroids/build-boot.sh xnu/out/kernel.development.sweet first`
(args `debug=0xc serial=3 cpus=1 keepsyms=1`) -> `fastboot boot` ->
**xnu reaches "Waiting on <dict>IOProviderClass IOMedia, Content Apple_HFS"**
(waiting for root, as intended; photo 15:13). Seen on screen (observed):
iBoot version sweet-shim; SWEET AMFI/vnguard/trust-cache lines; device-tree
platform expert registered; AKS, IOWatchdog, IOCoreSurfaceRoot, HID buttons +
touch services (no MMIO), battery static fallback; **"SWEET FB: AppleCLCD
1080x2392 stride 4320 at 0xe3940000, presented as 1125x2436"**; **"SWEET CPU:
cpu=0 mpidr=0x0 came alive; SGI1 OK"**, "1/1 CPUs initialized, SGI1 verified";
mbuf pool 196 MB; dlil/ifnet threads scheduled (timer IRQ through the vGIC
works); VM bootstrap done; no SWEET UFS line (no rd=, storage untouched).
"debug_log_init: Error!! gPanicBase is still not initialized" also appears
(as on sweet). The console wrapped once.

**15:15: `cpus=8` works** (`asteroids/build-boot.sh ... smp8 debug=0xc serial=3
cpus=8 keepsyms=1`, photo 15:15): "requested=8 selected=8 (limit=8)"; for
cpu1-7 "CPU_ON target=0xN00 entry_pa=0x8c148e000 context=N", "PSCI CPU_ON rc=0",
"mpidr=0xN00 came alive; SGI1 OK"; "8/8 CPUs initialized, SGI1 verified";
"mcache: 8 CPU(s)"; then the same root wait. No change was needed for Gunyah.

**15:17: user chose: wipe userdata for the iOS root** (option 1; flash still to
be confirmed once more right before it). Preparation on the Linux PC:
IPSW 16.7.16 20H392 -> `research/ipsw-x/iPhone10,3_16.7.16_20H392_Restore.ipsw`
(Apple CDN, 6171035648 B); `lzfse` and `apfs-fuse` (sgan81, LZBITMAP ok) built
from source into ~/.local/bin; user installed hfsprogs + libfuse3-dev.
`asteroids/mkroot.sh IPSW OUTDIR` (user runs it in a terminal: sudo) = mkiosroot.sh
+ fsck + sweet-root-tweaks.py + img2simg -> `ios16-root-20H392-a1.img/.simg`.
Then: d22ap.adt for /product (adt.py unwrap), boot image with
`rd=ufsroot0 ufsrw=1 rootrw=1 ...` (needs ASTEROIDS_ALLOW_UFS_WRITES=yes),
`fastboot -S 512M flash userdata ...simg`. Expect no touch/buttons (sweet
drivers are gated off on asteroids).

**15:32 image a1 built** (user ran mkroot.sh): `research/ipsw-x/ios16-root-20H392-a1.img`
(16 GB HFS+J case-sensitive, 12 GB used, fsck OK) + `.simg` 12145344808 B + `.sha256`.
Tweaks applied except "no mobileactivationd system container; skipped data ark"
(activation relies on the ADT Hactivation path). OS 087-86698-044, SystemOS
087-86593-047, AppOS 087-86715-047. `research/ipsw-x/d22ap.adt` extracted
(/product: iPhone X). Boot image `asteroids/out/root1-boot.img` (args: debug=0xc
serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0
unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1
rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_hz=30).

**USERDATA WIPED, user confirmed ("flash"): 10:03:38-10:09:15 UTC
`fastboot -S 512M flash userdata ...a1.img.simg`, 24/24 chunks OKAY**
(`research/flash-userdata-a1.log`). LineageOS data is gone; to restore Android:
`fastboot -w`. userdata = iOS root a1 until the first rw boot changes it.
10:09:48 UTC `fastboot boot asteroids/out/root1-boot.img` (sent OKAY).
User report (observed): console scrolled (root mounted from LUN 0 userdata),
then **the Apple logo appeared** (~10:15 UTC) - iOS userland drawing through
AppleCLCD on asteroids. Waiting for the lock screen (sweet: ~4-5 min on the logo).
**root2** (10:20 UTC, sweetfb_noconsole=1): Apple logo for 13+ min. Log pulled in
Lineage recovery (`fastboot reboot recovery`, adb root, /data NOT mounted,
userdata = sda13 there) with tools/sweet-log-pull.sh -> `logs/asteroids-root2.txt`
(154105 B, 77 flushes). Observed: SWEET UFS "userdata" on LUN 0 first block
0x256248 230237 MiB read-write, DMA 0x8c4f30000; "not sweet, no keys/touch/USB";
only crash driverkitd (4x, sig 9); bluetoothd/fairplayd.H2 relaunch loop (77x,
as on sweet); datamigrator ran 2x; lsd exec'd once. **SpringBoard main thread
identical in all 6 samples**: `-[CCSModuleRepository _initWithDirectoryURLs:...]`
-> `LSApplicationRecord initWithBundleIdentifier` -> `_LSContextInitCommon` ->
os_unfair_lock wait; holder t1: `_LSCopyServerStore` ->
`__LAUNCH_SERVICES_IS_RESEEDING_ITS_DATABASE_AND_MAY_BLOCK__` -> sync XPC to lsd.
= lsd rebuilds the LaunchServices database (first boot); unknown whether slow
(each reboot may restart it) or stuck. Symbolication: blacktop ipsw 3.1.739 in
the session scratchpad, cache `research/ipsw-x/dyld/20H392__iPhone10,3_6/`
(`ipsw extract --dyld --dyld-arch arm64 IPSW -o research/ipsw-x/dyld`),
unslid addr = addr - slide (slide from the "SMP proc[pid] slide" line).
**root3** (10:37:52 UTC) = root2 + `sweetsample=SpringBoard,lsd,backboardd`; let it
run ~30 min undisturbed, then pull the log: lsd stacks changing = progress.
Reference (2026-10-19): https://github.com/MakrSas/ios-topaz = fork of ios-sweet for
the Redmi Note 12 4G (topaz, SM6225), 110 patches, lock screen in ~3 min on 4 A73.
Its README: "the first boot spends a while on the Apple logo (SpringBoard waits for
LaunchServices to build its database)" -> our root2 stack is that step; theirs ends
in ~3 min, ours did not within 13 min. Same ABL v4-ramdisk drop / payload after
_end. Root tools identical to ours. Touch there: FocalTech FT5452 on I2C (0x38) via
sweet's GENI I2C code (patches 0083/0086/0090); ours is FocalTech on SPI. No LICENSE
in either repo: use findings, ask before copying code.

## >>> READ FIRST: handoff 2026-10-19 ~15:05 (state, next step, how to run) <<<

**Where we are.** Probes done; the boot chain up to xnu works on the phone
(loader test = solid green). Kernel patch 0082 is written but the kernel is
NOT built yet: the user started the xnu CI build (workflow `xnu.yml`, push of
`xnu/patches/` to main) and is moving to a Mac. Nothing has been flashed;
everything ran via `fastboot boot`. userdata still holds LineageOS.

**Next step (first xnu boot on asteroids):**
1. Get the CI kernel: `gh run download RUN_ID -R <repo> -n xnu-kernel -D xnu/out`
   (the user runs CI on their remote; `me` = danascape/ios-android). Use
   `kernel.development.sweet` (MACHINE_CONFIG SWEET; the same kernel serves both
   boards). Check the CI log says "Applying patch: .../0082-..." and NOT
   "Skipping patch 0082" (CI skips non-applying patches silently).
2. `asteroids/build-boot.sh xnu/out/kernel.development.sweet first`
   -> `asteroids/out/first-boot.img`. Default boot-args
   `debug=0xc serial=3 cpus=1 keepsyms=1`: no rd=, so no UFS root, no storage
   access at all.
3. Phone into fastboot (power + vol-), `fastboot boot asteroids/out/first-boot.img`.
4. Expected: navy shim screen (SSU, payload, JUMP), then xnu's early console on
   black ("SWEET EARLY 1..20" marks, only rows inside the 32MB block holding the
   fb start, i.e. the top ~1638 rows), then the fbcon console; it should end
   waiting for a root device. A white 64x32 band at bottom-left of that area =
   start.s stage 0 ran. Photograph the screen.
5. Likely trouble spots, in order: (a) something in early boot still assumes
   sweet (search the photo's last mark in osfmk/arm/arm_init.c); (b) GIC under
   Gunyah's vGIC (probe 2 showed the 0x13 GICD + 32-bit priority setup works);
   (c) timer INTID 27; (d) memory at 0x8c0000000 (pmap is fine with it per
   design, untested).
6. After that: `cpus=8` (secondaries enter at EL1 under Gunyah; sweet's entry
   already handles EL1), then the root filesystem question (needs userdata
   -> iOS: ask the user before writing anything; build-boot.sh refuses
   `rd=ufsroot0 ufsrw=1` unless ASTEROIDS_ALLOW_UFS_WRITES=yes).

**How to run things (any host):**
- Shim/probes: `asteroids/shim/build.sh PAYLOAD [OUT]`, `asteroids/probe/build.sh N`.
  Need clang + ld.lld + llvm-objcopy/nm/readelf (`LLVM=/path/to/llvm/bin`; on this
  Linux host the LineageOS prebuilt is the default; macOS: `brew install llvm lld`,
  `LLVM=$(brew --prefix llvm)/bin`, lld from `$(brew --prefix lld)/bin` may need
  to be symlinked next to it) and AOSP mkbootimg.py (`MKBOOTIMG=...`; e.g.
  `git clone https://android.googlesource.com/platform/system/tools/mkbootimg`).
  python3 for tools/*.py. Android platform-tools for fastboot/adb.
- `tools/xnu-tree.sh DIR` recreates the patched xnu tree (tag `patched`) to write
  patch 0083+ as `git diff patched`; verify it applies before pushing.
- Live DT: `asteroids/backup/live.dtb` (dtc: `dtc -I dtb -O dts`).
- Phone: LineageOS 24 userdebug, `adb root` works; slot b; fastboot boot works.
  The probe/shim screens hold forever (power + vol- to get back to fastboot).

**Not in git (stay on the Linux PC unless copied):** `asteroids/backup/` (14 GB,
device-specific: persist/modem calibration, serial in live.dtb - keep private),
the phone photos (results are transcribed below), LineageOS tree/toolchain.

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
