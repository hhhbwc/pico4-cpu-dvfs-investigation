#!/bin/bash
set -euo pipefail
KDIR="${KDIR:-/home/hhhbwc/linux-build/linux-4.19}"
M="${M:-/home/hhhbwc/linux-build/cpu_oc}"
SRC="${SRC:-/mnt/c/Users/wzy/ALCOM/Projects/sj/pico4-display-analysis/test_evidence/epss-lut-probe/cpu_oc_k1.c}"
OUTDIR="/mnt/c/Users/wzy/ALCOM/Projects/sj/pico4-display-analysis/test_evidence/epss-lut-probe/out"

command -v aarch64-linux-gnu-gcc >/dev/null || { echo "ERROR: aarch64-linux-gnu-gcc missing"; exit 1; }
[ -d "$KDIR" ] || { echo "ERROR: kernel tree not found"; exit 1; }
[ -f "$SRC" ]  || { echo "ERROR: source not found"; exit 1; }

aarch64-linux-gnu-gcc --version | head -1
echo "=== kernel: $(grep UTS_RELEASE "$KDIR/include/generated/utsrelease.h")"

mkdir -p "$M"
cp "$SRC" "$M/cpu_oc_k1.c"
printf 'obj-m += cpu_oc_k1.o\n' > "$M/Makefile"
rm -f "$M"/cpu_oc_k1.{ko,o,mod.o,mod.c,mod.order}

cd "$M"
make -C "$KDIR" M="$M" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     LOCALVERSION= SKIP_STACK_VALIDATION=1 EXTRA_CFLAGS="-Wno-error" \
     modules 2>&1 | tail -30

echo "=== RESULT ==="
ls -la "$M/cpu_oc_k1.ko" 2>&1 || { echo "ERROR: .ko not produced"; exit 1; }

echo "=== VERMAGIC ==="
VM=$(strings "$M/cpu_oc_k1.ko" | grep -m1 '^vermagic=' || echo NONE)
echo "$VM"
case "$VM" in
    *"4.19.81-perf+"*"modversions aarch64"*) echo "VERMAGIC MATCHES TARGET" ;;
    *) echo "WARNING: vermagic mismatch"; exit 2 ;;
esac

mkdir -p "$OUTDIR"
cp "$M/cpu_oc_k1.ko" "$OUTDIR/"
echo "=== copied to $OUTDIR/cpu_oc_k1.ko ==="
