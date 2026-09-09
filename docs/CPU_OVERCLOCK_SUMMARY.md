# CPU Overclock Summary

## Goal

Attempt to overclock CPU7 from the fused maximum of `2841600 kHz` toward approximately `3 GHz`.

## Result

The device did not reach `3 GHz`.

CPU7 remained capped at `2841600 kHz`.

## What was tested

One live EPSS perf-state write was performed on the CPU7 prime domain.

Target register:

```text
0x18593320
```

Attempted speculative L156 word:

```text
0x4004009c
```

Hardware readback:

```text
0x0000001c
```

## Interpretation

- EPSS did not accept the speculative L156 perf-state word
- no new operating point appeared
- CPU7 stayed at the fused maximum
- the device remained stable

## Why this matters

The write did not create a new frequency row.

The hardware path accepted only the existing registered state and did not honor an out-of-table value.

## Key constraint

The EPSS table ends at LVAL `148` = `2841600 kHz`.

There is no LVAL `156` row.

The voltage table also ends at index `19` = `904 mV`.

## Safety notes

- no boot image was flashed
- no EPSS table was rewritten
- no RPMh voltage table was rewritten
- the only live write was one CPU7 EPSS perf-state register write
- the device did not reboot

## Recommendation

Stop at the fused maximum:

```text
CPU0-3 = 1804800 kHz
CPU4-6 = 2419200 kHz
CPU7   = 2841600 kHz
```
