#!/bin/bash
#
# Build epss_lut_probe.ko for the PICO 4 kernel.
#
# Run this on the SAME buildroot that built dsi120.ko, i.e. the Linux machine
# that holds /home/hhhbwc/linux-build/linux-4.19 (see
# pico4_120hz-repo/pico4-display-analysis/dsi120/setup_buildroot.sh).
#
# The target vermagic must be exactly:
#   4.19.81-perf+ SMP preempt mod_unload modversions aarch64
#
# Flags are copied verbatim from the known-good dsi120 build:
#   SKIP_STACK_VALIDATION=1  bypass objtool entirely (never built in this tree)
#   -Wno-error               GCC 13 trips over 4.19's strict warning set; we
#                            only want real errors
#   LOCALVERSION=            version comes from localversion ("-perf+")
set -euo pipefail

SRC="${SRC:-/mnt/c/Users/wzy/ALCOM/Projects/sj/pico4-display-analysis/test_evidence/epss-lut-probe/epss_lut_probe.c}"
KDIR="${KDIR:-/home/hhhbwc/linux-build/linux-4.19}"
M="${M:-/home/hhhbwc/linux-build/epss_lut_probe}"

if [ ! -d "$KDIR" ]; then
    echo "ERROR: kernel tree not found at $KDIR"
    echo "       export KDIR=/path/to/linux-4.19 and re-run"
    exit 1
fi
if [ ! -f "$SRC" ]; then
    echo "ERROR: source not found at $SRC"
    exit 1
fi

mkdir -p "$M"
cp "$SRC" "$M/epss_lut_probe.c"

# Clean stale objects so modversions CRCs do not leak between builds.
rm -f "$M"/epss_lut_probe.{ko,o,mod.o,mod.c,mod.order}

cd "$M"
make -C "$KDIR" M="$M" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     LOCALVERSION= SKIP_STACK_VALIDATION=1 EXTRA_CFLAGS="-Wno-error" \
     modules 2>&1 | tail -40

echo "=== result ==="
ls -la "$M"

if [ -f "$M/epss_lut_probe.ko" ]; then
    echo "=== vermagic ==="
    strings "$M/epss_lut_probe.ko" | grep -m1 '^vermagic=' || echo "(no vermagic string found)"
    echo "=== copy for device ==="
    mkdir -p /mnt/c/Users/wzy/ALCOM/Projects/sj/pico4-display-analysis/test_evidence/epss-lut-probe/out
    cp "$M/epss_lut_probe.ko" /mnt/c/Users/wzy/ALCOM/Projects/sj/pico4-display-analysis/test_evidence/epss-lut-probe/out/
    echo "OK: $M/epss_lut_probe.ko"
else
    echo "ERROR: .ko not produced"
    exit 1
fi
