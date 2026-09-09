#!/bin/bash
# Build epss_lut_probe.ko inside WSL Ubuntu against the PICO 4 buildroot.
#
# Flags are copied verbatim from the known-good dsi120.ko build:
#   SKIP_STACK_VALIDATION=1  bypass objtool (never built in this tree)
#   -Wno-error               GCC 13 trips over 4.19's strict warning set
#   LOCALVERSION=            version comes from localversion ("-perf+")
#
# Required compiler (matches the dsi120.ko vermagic string exactly):
#   aarch64-linux-gnu-gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
#   sudo apt-get install -y gcc-aarch64-linux-gnu
set -euo pipefail

KDIR="${KDIR:-/home/hhhbwc/linux-build/linux-4.19}"
M="${M:-/home/hhhbwc/linux-build/epss_lut_probe}"
SRC="${SRC:-/mnt/c/Users/wzy/ALCOM/Projects/sj/pico4-display-analysis/test_evidence/epss-lut-probe/epss_lut_probe.c}"
OUTDIR="/mnt/c/Users/wzy/ALCOM/Projects/sj/pico4-display-analysis/test_evidence/epss-lut-probe/out"

for need in aarch64-linux-gnu-gcc; do
    command -v "$need" >/dev/null 2>&1 || { echo "ERROR: $need missing (apt-get install -y gcc-aarch64-linux-gnu)"; exit 1; }
done
[ -d "$KDIR" ] || { echo "ERROR: kernel tree not found at $KDIR"; exit 1; }
[ -f "$SRC" ]  || { echo "ERROR: source not found at $SRC"; exit 1; }

echo "=== compiler ==="
aarch64-linux-gnu-gcc --version | head -1
echo "=== kernel utsrelease ==="
cat "$KDIR/include/generated/utsrelease.h" | grep UTS_RELEASE

mkdir -p "$M"
cp "$SRC" "$M/epss_lut_probe.c"
printf 'obj-m += epss_lut_probe.o\n' > "$M/Makefile"
rm -f "$M/epss_lut_probe.ko" "$M/epss_lut_probe.o" "$M/epss_lut_probe.mod.o" \
      "$M/epss_lut_probe.mod.c" "$M/epss_lut_probe.mod.order"

cd "$M"
make -C "$KDIR" M="$M" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     LOCALVERSION= SKIP_STACK_VALIDATION=1 EXTRA_CFLAGS="-Wno-error" \
     modules 2>&1 | tail -35

echo "=== RESULT ==="
ls -la "$M/epss_lut_probe.ko" 2>&1 || { echo "ERROR: .ko not produced"; exit 1; }

echo "=== VERMAGIC ==="
VM=$(strings "$M/epss_lut_probe.ko" | grep -m1 '^vermagic=' || echo NONE)
echo "$VM"
case "$VM" in
    *"4.19.81-perf+"*"modversions aarch64"*) echo "VERMAGIC MATCHES TARGET" ;;
    *) echo "WARNING: vermagic differs from expected 4.19.81-perf+ ... modversions aarch64"; exit 2 ;;
esac

mkdir -p "$OUTDIR"
cp "$M/epss_lut_probe.ko" "$OUTDIR/"
echo "=== copied to $OUTDIR/epss_lut_probe.ko ==="
