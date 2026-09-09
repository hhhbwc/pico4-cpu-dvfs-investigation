# Detailed Investigation Notes

## Device

- PICO 4 / PICOA8110
- SoC: Qualcomm SM8250 / Kona / Snapdragon XR2
- CPU7: Cortex-A76 prime core
- Kernel: `4.19.81-perf+`
- USB serial: `PA8110MGGB070328G`
- Wireless ADB: `192.168.1.119:5555`

## Goal

Attempt to raise CPU7 from the fused maximum `2841600 kHz` toward approximately `3 GHz`.

## What was checked

### 1. Active DTB

The active DTB was inspected for CPU operating points, EPSS configuration, DCVSH settings, and clock references.

Result: no userspace-visible CPU OPP table in the active DTB path exposed a 3 GHz point.

### 2. Running kernel disassembly

The running kernel image was disassembled to confirm the driver paths and frequency math.

Important driver:

- `qcom-cpufreq-hw`
- EPSS frequency calculation path:

```text
freq_kHz = clock_hz * LVAL / 8000 / 1000
```

The active XO clock reference resolves to `153600000 Hz` for the calculation used by the driver.

### 3. Sysfs CPUFreq

The exposed prime CPU7 table topped out at `2841600 kHz`.

Available frequencies shown by sysfs were evenly spaced, but that does not mean every entry maps to a distinct EPSS row.

### 4. Read-only EPSS LUT dump

The `epss_lut_probe` module read the EPSS frequency and voltage tables.

Prime domain table:

```text
LVAL  44 =  844800 kHz
LVAL  68 = 1305600 kHz
LVAL  91 = 1747200 kHz
LVAL 113 = 2169600 kHz
LVAL 133 = 2553600 kHz
LVAL 148 = 2841600 kHz
LVAL 148 = 2841600 kHz  # duplicate/end marker
```

Key result:

- the table ends at LVAL `148`
- there is no LVAL `156` row
- `153600000 * 156 / 8000` would be approximately `2995200 kHz`
- that value is not implemented in the populated EPSS table

### 5. Voltage table

The prime voltage table ended at index `19` = `904 mV`.

There was no index `20` row available for a higher operating point.

### 6. DCVSH / hardware clamp registers

The module also inspected registers above the normal driver paths.

Examples for the prime domain:

```text
0x34c = 0x0000ccc8
0x3c0 = 0x00008000
```

These values indicate active hardware limit behavior.

The driver does not expose a userspace setter for these paths.

### 7. Live write test

The `cpu_oc_k1` module attempted one bounded write to the CPU7 EPSS perf-state register.

Register:

```text
0x18593320
```

Attempted speculative word:

```text
0x4004009c
```

Readback:

```text
0x0000001c
```

Interpretation:

- the hardware did not accept the speculative L156 perf-state word
- no new L156 operating point appeared
- CPU7 remained capped at `2841600 kHz`
- the device remained stable

## Why 3 GHz is blocked

There are at least three independent blockers.

### Blocker 1: no EPSS frequency row for L156

The populated table stops at LVAL `148`.

That means `2841600 kHz` is the highest implemented EPSS frequency point for CPU7.

### Blocker 2: voltage table has no higher row

The voltage table stops at index `19` = `904 mV`.

There is no exposed voltage row for a higher operating point.

### Blocker 3: hardware clamps are active

DCVSH/hardware limit registers are populated.

They are not controlled by the exposed Linux userspace path.

## What was not done

- no boot image was flashed for CPU overclocking
- no EPSS table was rewritten
- no RPMh voltage table was rewritten
- no firmware was modified
- no TZ/XBL/PMIC configuration was modified

## What would be required to continue

To attempt beyond this point, you would need a full custom plan:

1. custom kernel driver
2. custom EPSS table handling
3. custom voltage handling
4. PLL/firmware analysis
5. explicit rescue boot procedure
6. staged hardware validation

That is outside the scope of safe reproduction on the stock device.

## Recommendation

Stop at:

```text
CPU0-3 = 1804800 kHz
CPU4-6 = 2419200 kHz
CPU7   = 2841600 kHz
```

## Safety

Hardware write experiments can be destructive even when they appear to work.

Before reproducing:

- keep a verified boot backup
- confirm ADB recovery works
- understand the rescue procedure
- avoid writing firmware or voltage tables without an explicit recovery plan
