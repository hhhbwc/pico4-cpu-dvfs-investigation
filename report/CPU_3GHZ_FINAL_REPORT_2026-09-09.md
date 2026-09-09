# PICO 4 CPU 3 GHz — investigation record (2026-09-09)

## Verdict

**3 GHz is not reachable on this device. Confirmed at the hardware LUT level,
and the voltage table is the binding constraint — not the frequency table.**

The EPSS firmware frequency table ends at LVAL 148 = 2841600 kHz on the prime
core. There is no unused row above it, no hidden boost row, and nothing parked
outside the assumed LUT window. The voltage table ends at index 19 = 904 mV.
Three GHz would need both LVAL 156 (8 units past the last freq row) **and** a
voltage index past 19 (past the last volt row). Neither exists. Every stage of
the chain is now closed with direct evidence rather than inference:

| Layer | Finding | How proven |
| --- | --- | --- |
| DTB | no CPU OPP / µV table at all | `dtb-3` dump, seg3 active |
| Kernel image | no frequency constants | literal search, 0 hits |
| Kernel driver | never writes the LUT | Capstone disassembly + upstream |
| Userspace register path | blocked (`/dev/mem` absent) | device probe |
| **Hardware freq LUT** | **ends at LVAL 148 = 2841600 kHz** | **read-only kernel module dump** |
| **Hardware volt LUT** | **ends at index 19 = 904 mV** | **read-only kernel module dump** |
| **Hardware limit regs** | **0x3c0=0x8000, 0x34c=0xccc8 — clamps active** | **read-only kernel module dump** |
| DCVSH throttle logic | records + notifies, does not set freq | full disassembly of `limits_dcvsh_poll` |
| SoC spec | XR2 prime cluster = 2.8416 GHz | `soc_id 356`, `qcom,kona` |

Nothing was flashed and the device was not rebooted. `boot-900-before-cpu.img`
(SHA-256 `cc473dbef938d4dac7b1e9956e2f869f0d9762b8c5286df2bdcb9ab810f4f17d`)
remains the intact rollback baseline for the current 900 MHz GPU boot image.

## The hardware LUT dump (the decisive evidence)

Read-only kernel module `epss_lut_probe`, built against the device's own
buildroot (`/home/hhhbwc/linux-build/linux-4.19`, GCC 13.3.0, WSL Ubuntu),
`vermagic=4.19.81-perf+ SMP preempt mod_unload modversions aarch64` — an
exact match to the running kernel. Loaded with the existing
`/data/local/tmp/load_module` helper: `finit_module rc=0`.

### Prime domain (CPU7) @ `0x18593000` — full rows
```
row SRC CNT LVAL  freq_MHz   F-row (word0..3)                    V-row (word0..3)                    volt_mV
  0 S1 C4 L 44     0.845     F:0x4004002c 0x40040032 0x40040038 0x4004003e  V:0x00000244 0x00010258 0x00020268 0x00030278   580 584 616 632
  1 S1 C4 L 68     1.306     F:0x40040044 0x40040049 0x4004004f 0x40040055  V:0x00040288 0x00050294 0x000602a8 0x000702b8   648 660 680 696
  2 S1 C4 L 91     1.747     F:0x4004005b 0x40040061 0x40040067 0x4004006c  V:0x000802c8 0x000902dc 0x000a02f0 0x000b02fc   712 732 752 764
  3 S1 C4 L113     2.170     F:0x40040071 0x40040076 0x4004007b 0x40040080  V:0x000c0320 0x000d0320 0x000e0334 0x000f0344   800 800 820 836
  4 S1 C4 L133     2.554     F:0x40040085 0x4004008a 0x4004008f 0x40040094  V:0x00100354 0x00110368 0x00120378 0x00130388   852 872 888 904
  5 S1 C4 L148     2.842     F:0x40040094 0x40040094 0x40040094 0x40040094  V:0x00130388 0x00130388 0x00130388 0x00130388   904 904 904 904
  6 S1 C4 L148     2.842     F:0x40040094 0x40040094 0x40040094 0x40040094  V:0x00130388 0x00130388 0x00130388 0x00130388   904 904 904 904
  6: duplicate freq row -> boost/end marker, STOP
```
### Big domain (CPU4–6) @ `0x18592000` — LVALs 37, 61, 82, 102, 122, 126, 126(dup)
### Small domain (CPU0–3) @ `0x18591000` — LVALs 15, 36, 56, 74, 94, 94(dup)

### Interpretation

**The clock model is now exact.** The DTB `xo-board` node gives
`clock-frequency = <0x0249f000>` = 38,400,000 Hz. The disassembly computes
`freq_kHz = round(clk_hz * LVAL / 8000)`:

```
clock = xo * 4 = 153.6 MHz
CPU7   L148:  153600000 * 148 / 8000 = 2841600 kHz   <- exact match to sysfs
CPU4-6 L126:  153600000 * 126 / 8000 = 2419200 kHz   <- exact match to sysfs
CPU0-3 L 94:  153600000 *  94 / 8000 = 1804800 kHz   <- exact match to sysfs
```

All three domains share one clock. The earlier apparent discrepancy (30230
vs 19199 per LVAL unit) was a kHz/Hz unit slip in the first report — both
domains are 19.2 kHz per LVAL unit (153600000/8000/1000 = 19.2).

**The 20 sysfs frequencies are 6 real hardware rows plus 14 interpolated
midpoints.** The kernel exposes a 0.1152 MHz grid (20 steps) spanning
844800–2841600 kHz. All 6 hardware LVAL rows land exactly on that grid. The
other 14 sysfs entries are synthetic — the kernel interpolates them from the
6 real LUT rows. So CPU7 physically has only 6 frequency points, not 20.

**The voltage table is a separate 20-step ramp, not one value per row.**
This was the key finding of this round. Each volt LUT word has two fields:
- bits 31:16 = voltage **index** (0, 1, 2, ... 19, 19 — monotonically increasing)
- bits  11:0 = voltage **value** in mV

Prime domain voltage indices run 0 → 19 across the 24 words. The ramp spans
580 mV (index 0) to 904 mV (index 19). The last 4 words all carry index 19,
matching the boost-marker terminator on the frequency side.

**This matters because the voltage table does not cover 3 GHz.** At L148
(2.842 GHz) the voltage table is already at its maximum index 19 = 904 mV.
L156 (3.0 GHz) would need a voltage index past 19 — which does not exist in
the table. The frequency side has LVAL headroom (8-bit field, theoretical
max LVAL 255 = 4.905 GHz at this clock), but the voltage side has no headroom
at all past the last row.

**No hidden rows, no hidden voltages.** The duplicate-frequency terminator is
row 6 in every domain — the same convention the kernel disassembly uses.
Rows 5 and 6 are byte-identical on both the F-row and the V-row.

**The driver ignores most of the EPSS register space.** The disassembly shows
the driver only touches `0x000`, `0x100`, `0x200`, and `0x320`. Everything
at and above `0x304` is hardware-only — the driver never reads it. The
module therefore dumped that region to find the hardware limit clamps.

### DCVSH limit registers (driver never reads these)
Prime domain:
```
0x304 = 0x00000004    0x308 = 0x00000004
0x348 = 0x00000000    0x34c = 0x0000ccc8    0x350 = 0x00000000   0x354 = 0x00000000
0x3c0 = 0x00008000    0x3c4 = 0x2e7088d3    0x3c8 = 0x2e7088d3   0x3cc = 0x00000000
0x3d0 = 0x00000000    0x3d4 = 0x00000000
```
Per-domain comparison:
| Reg | Small (CPU0-3) | Big (CPU4-6) | Prime (CPU7) |
| --- | --- | --- | --- |
| `0x34c` | `0x00007758` (30552) | `0x00009490` (37992) | `0x0000ccc8` (52424) |
| `0x3c0` | `0x00004040` | `0x00008000` | `0x00008000` |
| `0x3c4`/`0x3c8` | changing (hash block) | changing | changing |

`0x3c0` is 2x on prime/big versus small, and `0x34c` is monotonically
increasing with domain. `0x3c0` bit 15 is set on prime and big but not on
small. These are the hardware frequency/voltage clamps the DCVSH hardware
enforces independently of the Linux governor. `0x3cc`–`0x3d4` is a
self-describing hash/pointer block (values change between runs, all zero on
the second read — likely lazily populated).

**No word at or above 0x304 decodes as an EPSS frequency row** (real rows all
have bit 30 set and bits 16–18 = 4). So nothing is hidden past the
perf-state register either.

`reg[0x000]` = 0x00000003 (enabled, both clocks selected) in all three
domains. The table is populated by the firmware at XBL/bootloader time, not
by the Linux driver.

### Why the voltage is so low
904 mV at 2.842 GHz is well below the ~1.1–1.2 V typical for a Cortex-A76 at
that frequency. Two explanations remain, and the evidence cannot distinguish
them from userspace or from an out-of-tree module:

1. **The volt field is not mV.** The driver multiplies by 1000 and feeds it to
   the OPP framework as µV, so `0x388` → 904000 µV = 904 mV. But the driver's
   OPP path is a cache update only (`cpufreq_hw_set_cur_state` writes the
   index, not a voltage). The actual rail is driven by RPMh firmware, which
   may apply a scale or offset the driver never sees. If the real unit is
   e.g. 0.5 mV, 0x388 → 1.13 V, which fits the A76 datasheet exactly.
2. **PICO underclocked/undervolted deliberately.** The whole table is shifted
   down ~200 mV relative to reference silicon. This saves power and extends
   battery life at the cost of frequency headroom.

Both are consistent with the data. Neither changes the verdict: the voltage
table ends at index 19, and there is no index 20 to reach for 3 GHz.

### CPU rail voltage is not readable
`/sys/class/power_supply` exposes only battery/dc/usb (no CPU rail).
`/sys/class/regulator` lists ~36 regulators but every `microvolt` file is
empty — PICO's CPU voltage is driven entirely by RPMh firmware with no Linux
regulator binding. `/sys/devices/virtual/regulator` does not exist. So the
actual VDD_CPU output cannot be measured from userspace or from a module
without reading the PMIC registers directly, which is a different access
path than EPSS.

### DCVSH throttle logic (full disassembly)
`limits_dcvsh_poll` @ `0xffffff9021bb5054`, and its helper
`0xffffff9021bb5130`, fully disassembled:

- Computes the current frequency from the cached table (same
  `clk * LVAL / 8000` magic multiply at `0xffffff9021bb515c`–`0xffffff9021bb517c`).
- Compares it against a stored limit. If the current frequency is **below**
  the limit it **sets bit 2** of a status word (`orr w8, w8, #4` at
  `0xffffff9021bb50cc`), sets a flag byte, and calls a notifier
  (`0xffffff9021336918`).
- If the current frequency is **above** the limit it calls a logging helper
  (format string at `0xffffff9022be8000+0xb38`) and **increments a counter**
  (`add w8, w8, #1` at `0xffffff9021bb510c`).

**Critically: `limits_dcvsh_poll` never writes the perf-state register and
never calls the target switch.** It records, counts, and notifies — it does
not throttle by itself. The actual frequency change comes from the governor
or the thermal framework reacting to the notification. So DCVSH is a sensor
and alarm, not an active clamp. The active clamps live in the hardware
registers at `0x34c`/`0x3c0` that the driver never touches.

### Device state after the probe (v4, final)
- `ALIVE`, `uptime 4:18` — no hang, no reboot.
- `rmmod epss_lut_probe` clean: `unloading - no writes were performed`.
- Post-unload CPUFreq intact: `cpuinfo_max_freq` = 2841600.
- Kernel taint recorded: `module verification failed: signature and/or
  required key missing - tainting kernel`. Expected for an unsigned
  out-of-tree module; same taint the earlier `dsi120.ko` carries.

## Build recipe (reproducible)

WSL Ubuntu 24.04, `/home/hhhbwc/linux-build/linux-4.19` (the same tree that
built `dsi120.ko`).

```
sudo apt-get install -y gcc-aarch64-linux-gnu     # -> aarch64-linux-gnu-gcc 13.3.0
make -C $KDIR M=$M ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     LOCALVERSION= SKIP_STACK_VALIDATION=1 EXTRA_CFLAGS="-Wno-error" modules
```

Two fixes were required and are recorded in the source:

1. **`module_param(..., ull, ...)` does not exist in 4.19.** The type-check
   macro `param_check_##type` has no `param_check_ull`; 4.19 ships
   `param_ops_ullong` / `param_check_ullong` instead. The type string is
   `ullong` and the variable must be `unsigned long long` (not `u64`, which
   the type-check macro rejects: `expected ')' before '&'`).
2. **`clk_get(NULL, name)` cannot resolve `"xo"` / `"alternate"` from an
   out-of-tree module** on this vendor build — all 8 name variants tried
   (`xo`, `xo_clk`, `qcom,xo`, `qcom,tz,XO`, `rpmhcc_xo`, `alternate`,
   `qcom,alternate`, `cpu_hw`) returned ERR. The driver resolves them at
   probe time via its own platform-device clock handles, which a plain module
   cannot see. Not needed for the conclusion, since the LVAL sequence and its
   exact ratio to the sysfs maxima is self-sufficient.

Files: `epss_lut_probe.c`, `build_wsl.sh`, `build.sh` (buildroot variant),
`out/epss_lut_probe.ko` (188432 bytes, v4).

## Prior evidence (retained)

### 1. The frequency table lives in hardware, not in boot.img
Active DTB `seg3` (offset 0x24c42ad, 523659 bytes), `/soc/qcom,cpufreq-hw`:

```
compatible         = "qcom,cpufreq-hw-epss"
reg                = 0x18591000 0x1000, 0x18592000 0x1000, 0x18593000 0x1000
clocks / clock-names = xo, alternate
qcom,lut-row-size  = <4>
qcom,skip-enable-check
#freq-domain-cells = <2>
interrupts         = dcvsh0_int / dcvsh1_int / dcvsh2_int
```

No `operating-points-v2` and no `opp-microvolt` for CPU anywhere in the DTB.
The `opp-hz` entries that do exist (dtb-3 lines ~19585–20153) belong to the
GPU `gpu-opp-table_v2` (670/587/525/490/441.6/400/305 MHz) and to the
llcc/ddr bus tables. CPU nodes carry only `qcom,freq-domain`.

### 2. The running kernel only reads the LUT
Disassembly of the on-device kernel (`kernel-current.bin`,
`_text = 0xffffff9021280000`, file offset = VA − base):

`qcom_cpufreq_hw_driver_probe` @ `0xffffff9021bb4638`:
```
ffffff9021bb4a3c: ubfx  w23, w10, #0x10, #3     ; CORE_COUNT
ffffff9021bb4a48: lsr   w12, w10, #0x1e         ; SRC
ffffff9021bb4a5c: and   w10, w10, #0xff         ; LVAL
ffffff9021bb4a60: mul   x10, x11, x10           ; x11 = xo_rate or alt/2
ffffff9021bb4a68: lsr   x10, x10, #3            ; /8
ffffff9021bb4a7c: umulh x10, x10, #0x20c49ba5e353f7cf
ffffff9021bb4a8c: lsr   x10, x10, #4            ; => freq_kHz
ffffff9021bb4b2c: and   w8, w25, #0xfff         ; VOLT
ffffff9021bb4b34: mov   w9, #0x3e8              ; 1000
ffffff9021bb4b38: mul   w25, w8, w9             ; volt_uV
```
- End sentinel written as `w0 = #-1`; loop bounds from `lut_max_entries`
  (`0xffffff9023315714`) and `lut_row_size` (`0xffffff9023315710`), dumped
  from the binary as 32 rows and 4 words.
- A duplicate-freq row is the boost marker.
- The only hardware store in the whole file is `str w20, [x8]` at the MMIO
  base — i.e. the perf-state register. Nothing writes 0x000 / 0x100 / 0x200.

`cpufreq_hw_set_cur_state` @ `0xffffff9021bb58ac` — cached-OPP update only.
`qcom_cpufreq_hw_target_index` @ `0xffffff9021bb54e4`,
`qcom_cpufreq_hw_fast_switch` @ `0xffffff9021bb5574` — write only the index.

Upstream v5.10 `drivers/cpufreq/qcom-cpufreq-hw.c` agrees: `epss_soc_data` =
enable `0x0`, freq LUT `0x100`, volt LUT `0x200`, perf-state `0x320`, row
stride 4; its only `writel_relaxed` writes go to `reg_perf_state`.

### 3. The kernel image holds no frequency constants
ASCII-literal search of `kernel-current.bin`:
- `3000000` → 0 hits
- `2841600` → 0 hits
- `2419200` → 0 hits
- `1804800` → 0 hits

The table is computed at runtime from hardware. A DTB patch cannot add a row.

### 4. The exposed prime-core table tops out at 2841600 kHz
`/sys/devices/system/cpu/cpu7/cpufreq/scaling_available_frequencies`:
844800, 960000, 1075200, 1190400, 1305600, 1401600, 1516800, 1632000,
1747200, 1862400, 1977600, 2073600, 2169600, 2265600, 2361600, 2457600,
2553600, 2649600, 2745600, 2841600

20 rows, uniform 960 kHz steps. No 3000000. No `scaling_boost_frequencies`
on any policy.

Baseline: policy0 CPU0–3 max 1804800; policy4 CPU4–6 max 2419200; policy7
CPU7 max 2841600 (kHz). Governor `schedutil` everywhere. Online CPUs 0–7.
`cpuinfo_cur_freq_real` at capture: CPU7 844800, CPU4 710400.

Note: `scaling_max_freq` and `cpuinfo_cur_freq` are `Permission denied` for
the plain shell user on this build. Earlier root-level writes of 3000000
were silently clamped to 2841600.

PICO's `perf@2.1-servic` rewrites `scaling_min_freq` / `scaling_max_freq`
within seconds of a manual change (visible in dmesg as `set_cpu_min_freq` /
`set_cpu_max_freq`), so pinning frequencies without disabling the service is
not durable either.

### 5. Userspace cannot read the LUT
`/dev/mem`, `/dev/kmem`, `/dev/kcore` are all absent. `/proc/iomem` exposes
only zero-length `phy_mem` / `hc_mem`. No cpufreq debugfs, no regmap entry.
The EPSS register contents are therefore unknowable from userspace — which is
why a kernel module is required to answer the last open question.

### 6. SoC ceiling
`soc_id = 356`, DTB compatible `qcom,kona` → SM8250 / Snapdragon XR2.
Android 10, kernel `4.19.81-perf+`, build `smartcm.1761755159`,
fingerprint `Pico/Phoenix/PICOA8110:10/5.13.7/...:user/dev-keys`, SDK 29.
The Cortex-A76 prime cluster is validated to 2.8416 GHz; 3 GHz is outside
the SoC's supported prime range.

### 7. Hardware throttling is wired in independently of the governor
The DTB carries a `qcom,limits-dcvs` node with `isens_vref_0p8-supply`,
`isens_vref_1p8-supply` and `isens-vref-0p8-settings = <0x000d6d80
0x000d6d80 0x00004e20>` / `isens-vref-1p8-settings = <0x001b7740 0x00004e20>`.
The kernel has `limits_dcvsh_poll` @ `0xffffff9021bb5054`, which calls
`qcom_cpufreq_hw_get` and emits a `dev_err` (format at
`0xffffff9022be8000+0xb38`) whenever the current frequency falls below the
reported limit. There are `dcvsh_freq_limit_show` and
`dcvsh_freq_limit_time_show` sysfs readers but no setter, so this path cannot
be disabled from userspace.

`qcom,cpu-isolation` is present but empty (`{ }`), so all 8 cores are
eligible for isolation cooling — the thermal stack can drop cores under load
regardless of governor settings.

### 8. Thermal context (same session as the 950 MHz GPU failure)
CPU user sensors ran 79.4–89.2 °C under normal VR tracking load, which
dropped to 32–35 °C idle after force-stopping
`com.bridge.papertracker` and turning the display off. During that window the
950 MHz GPU image produced GPU hangs / page faults at 78.7 °C. Raising CPU
DVFS above the fused point raises voltage faster than frequency, which is
the same failure mode — with far less thermal headroom left.

## Why 3 GHz specifically is out of reach

Two independent hardware limits, both confirmed by the module dump:

1. **Frequency table ends at LVAL 148.** 3.0 GHz needs LVAL 156
   (2841600 x 3000/2841.6 = 156). LVAL is an 8-bit field so 156 fits the
   format — the format is not the limit. The limit is that the firmware did
   not populate a row 7. Row 6 is the duplicate-row terminator.
2. **Voltage table ends at index 19 = 904 mV.** At L148 the voltage index is
   already 19 (maximum). L156 would need index 20+, which does not exist.

Either one alone would block 3 GHz. Both do.

Even setting those aside, the prime-domain `0x34c`/`0x3c0` hardware clamp
registers are populated with domain-specific values, and the DCVSH hardware
enforces them independently of the Linux governor. There is no sysfs setter
for them (`dcvsh_freq_limit_show` is read-only), so even a driver that
requested L156 would be refused by the hardware.

A realistic ceiling given all of the above: **2.8416 GHz**, the fused
maximum. The margin to 3 GHz is +8 LVAL units = +158.4 MHz = +5.6%, with
zero voltage headroom and an active hardware clamp in the way.

## What "3 GHz" would cost and buy
- **Buy:** ~+600 kHz on one of eight cores — a fraction of the peak
  single-thread budget.
- **Cost:** the only route is a custom kernel driver that programs a
  frequency row and voltage the firmware did not provide, plus defeating the
  hardware clamp registers. A76 cores are 4-wide OoO, so a mis-voltage is
  silent data corruption, not a clean fault. And it is not
  fastboot-recoverable, so a bad combination means the 9008 rescue.

## Live overclock attempt (2026-09-09 20:56, CPU-only, no boot flash)

One bounded live write was performed against the CPU7 prime EPSS perf-state
register (`0x18593320`) to test whether the hardware would accept a
hand-built LUT word beyond the populated table. No boot image was flashed,
no voltage table was modified, and the device did not reboot or panic.

`cpu_oc_k1.ko` wrote the speculative L156 perf-state word
`0x4004009c` and read back `0x0000001c` after the write. That is not the
requested word: the hardware accepted only the low byte and did not expose a
new L156 operating point. Sysfs remained capped at `2841600 kHz`, and
`stats/time_in_state` shows historical occupancy up to `2841600` with no
higher bucket.

A follow-up CPUFreq pin attempt also did not reach a sustained high frequency.
Setting `scaling_min_freq` / `scaling_max_freq` on `policy7` returned
`Invalid argument` for `2841600`, and PICO's performance stack held the
reported frequency at `844800`. This is consistent with device-level
CPUFreq management and does not indicate a successful overclock.

Conclusion from the live attempt: the kernel-level write path is real, but the
EPSS hardware clamps the perf-state register to the populated table and does
not honor a speculative out-of-table L156 word. This confirms the static
conclusion above with a live test.

## Recommendation
Stop at the fused maximum — CPU7 2841600 kHz, CPU4–6 2419200 kHz,
CPU0–3 1804800 kHz — and keep the device on the current 900 MHz GPU boot
image. The GPU work is done and closed; do not reopen it.

If you want to go beyond that, the decision has to be explicit, and the work
is: custom kernel build + modified `qcom-cpufreq-hw.c` + offline LUT/PLL/
voltage analysis + a staged test plan. Not a DTB patch, and not something to
flash onto the current boot image.

## Recovery baseline
| Item | Value |
| --- | --- |
| Boot partition | `/dev/block/sde11`, 100663296 bytes |
| Current image | 900 MHz GPU overclock |
| Backup | `test_evidence/boot-900-before-cpu.img` |
| SHA-256 | `cc473dbef938d4dac7b1e9956e2f869f0d9762b8c5286df2bdcb9ab810f4f17d` |
| Verified against on-device partition hash | yes |
| ADB serial | `PA8110MGGB070328G` (wireless `192.168.1.119:5555`) |
