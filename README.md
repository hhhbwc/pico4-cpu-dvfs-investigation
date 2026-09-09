# PICO 4 CPU DVFS Investigation

Investigation of CPU DVFS and overclock limits on a PICO 4 headset, targeting the CPU7 / Cortex-A76 prime core on Qualcomm Snapdragon XR2 / SM8250 / Kona.

Target: raise CPU7 from the fused maximum of `2841600 kHz` toward approximately `3 GHz`.

Result: `3 GHz` is not reachable on this device through the stock EPSS path.

## Summary

The PICO 4 CPU7 prime core is capped at `2841600 kHz`.

The hardware EPSS frequency table ends at LVAL `148`:

- `153600000 Hz * 148 / 8000 = 2841600 kHz`
- `3 GHz` would require LVAL `156`
- no populated EPSS row exists for LVAL `156`
- the voltage table ends at index `19` = `904 mV`
- hardware clamp registers are active
- a live write attempt to EPSS perf-state did not produce a new operating point

No boot image was flashed for the CPU work. The device did not brick.

## Evidence

The investigation used:

- active DTB inspection
- on-device kernel disassembly
- sysfs CPUFreq analysis
- read-only kernel module EPSS LUT dump
- live CPU overclock attempt via out-of-tree kernel module
- post-attempt stability check

Final report:

- `report/CPU_3GHZ_FINAL_REPORT_2026-09-09.md`

Raw EPSS dumps:

- `evidence/EPSS_LUT_DUMP_2026-09-09.txt`
- `evidence/EPSS_LUT_DUMP_V4_2026-09-09.txt`

## Kernel modules

### `epss_lut_probe`

Read-only EPSS/DCVSH LUT probe.

Purpose:

- dump EPSS frequency table
- dump EPSS voltage table
- inspect DCVSH/limit registers
- confirm whether hidden rows or voltages exist

Files:

- `source/epss_lut_probe.c`
- `artifacts/epss_lut_probe.ko`

### `cpu_oc_k1`

CPU overclock test module.

Purpose:

- test whether EPSS would accept a speculative out-of-table perf-state word
- target CPU7 prime domain
- attempt LVAL `156` / approximately `3 GHz`
- read back the perf-state register

Observed result:

- wrote speculative L156 word `0x4004009c`
- hardware read back `0x0000001c`
- no L156 operating point was exposed
- CPU7 remained capped at `2841600 kHz`

Files:

- `source/cpu_oc_k1.c`
- `artifacts/cpu_oc_k1.ko`

## Build

Builds were done in WSL Ubuntu 24.04 against the device-matched Linux `4.19.81-perf+` buildroot.

```bash
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-
LOCALVERSION=
SKIP_STACK_VALIDATION=1
EXTRA_CFLAGS="-Wno-error"
```

Expected vermagic:

```text
4.19.81-perf+ SMP preempt mod_unload modversions aarch64
```

Scripts are included in:

- `scripts/build.sh`
- `scripts/build_wsl.sh`
- `scripts/build_cpu_oc.sh`

## Findings

### Prime CPU7 EPSS frequency table

```text
LVAL  44 =  844800 kHz
LVAL  68 = 1305600 kHz
LVAL  91 = 1747200 kHz
LVAL 113 = 2169600 kHz
LVAL 133 = 2553600 kHz
LVAL 148 = 2841600 kHz
LVAL 148 = 2841600 kHz  # duplicate/end marker
```

There is no LVAL `156` row.

### Prime CPU7 EPSS voltage table

Voltage table ends at index `19` = `904 mV`.

No index `20` exists.

### Conclusion

`3 GHz` is blocked by at least three independent limits:

1. EPSS frequency table has no L156 row
2. voltage table has no headroom past the last row
3. DCVSH/hardware clamp registers are active and are not controllable from userspace

## Safety notes

This repository contains hardware write experiments.

The CPU overclock module was used on a rooted device only.

Important facts:

- no CPU boot image was flashed
- the device did not reboot during the CPU write test
- no EPSS LUT table was rewritten
- no RPMh voltage table was rewritten
- the only live write was one CPU7 EPSS perf-state register write

If reproducing this, keep a verified boot backup and understand that EPSS, RPMh, DCVSH, XBL, TZ, and PMIC changes can require rescue boot hardware.

## Recovery baseline

| Item | Value |
| --- | --- |
| Device | PICO 4 / PICOA8110 |
| USB serial | `PA8110MGGB070328G` |
| Wireless ADB | `192.168.1.119:5555` |
| SoC | Qualcomm SM8250 / Kona / Snapdragon XR2 |
| Kernel | `4.19.81-perf+` |
| CPU7 fused max | `2841600 kHz` |
| Boot partition | `/dev/block/sde11` |
| Boot backup | `test_evidence/boot-900-before-cpu.img` |
| Backup SHA-256 | `cc473dbef938d4dac7b1e9956e2f869f0d9762b8c5286df2bdcb9ab810f4f17d` |

## Recommendation

Stop at the fused maximum:

- CPU0-3: `1804800 kHz`
- CPU4-6: `2419200 kHz`
- CPU7: `2841600 kHz`

Do not attempt further CPU overclocking on this device without a full custom kernel, firmware, PLL, voltage, and rescue plan.
