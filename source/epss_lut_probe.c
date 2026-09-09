/*
 * epss_lut_probe.c - read-only EPSS/DCVSH frequency LUT dump for PICO 4
 *                     (SM8250 / Kona, A8110)
 *
 * Purpose
 * -------
 * The CPU frequency table on this SoC is neither in the DTB nor in the kernel
 * image.  It is read at probe time by drivers/cpufreq/qcom-cpufreq-hw.c from
 * the EPSS hardware look-up tables:
 *
 *   base+0x000  enable / control
 *   base+0x100  frequency LUT   (row stride = 4 words, qcom,lut-row-size = <4>)
 *   base+0x200  voltage   LUT   (same stride)
 *   base+0x320  perf-state register   <- the ONLY register the driver writes
 *
 * The three bases come from /soc/qcom,cpufreq-hw in the active DTB (dtb-3):
 *   0x18591000  freq-domain 0  (CPU0-3,  cap 1804800 kHz)
 *   0x18592000  freq-domain 1  (CPU4-6,  cap 2419200 kHz)
 *   0x18593000  freq-domain 2  (CPU7,    cap 2841600 kHz)
 *
 * EPSS row encoding, confirmed by disassembling qcom_cpufreq_hw_driver_probe
 * at VA 0xffffff9021bb4638 in this device's kernel-current.bin:
 *
 *   bits 31:30  SRC        0 -> alternate_clk / 2 ; non-zero -> xo_rate*LVAL/1000
 *   bits 18:16  CORE_COUNT
 *   bits  7:0   LVAL
 *   volt row    bits 11:0  VOLT, in units of 1000 uV  (volt_uV = VOLT * 1000)
 *
 * The probe at 0xffffff9021bb4a48 does:
 *     lsr w12, w10, #0x1e          ; SRC
 *     ubfx w23, w10, #0x10, #3     ; CORE_COUNT
 *     lsr w10, w10, #3             ; then freq = (LVAL/8) * magic /1000
 * and at 0xffffff9021bb4b2c:
 *     and w8, w25, #0xfff
 *     mov w9, #0x3e8               ; 1000
 *     mul w25, w8, w9              ; voltage = VOLT * 1000
 *
 * Termination: a duplicate frequency row is the boost/end marker; an all-zero
 * row ends an empty table.  The kernel stores an end sentinel of -1.
 *
 * Safety
 * ------
 * READ-ONLY.  This module never writes any EPSS register, never writes the
 * perf-state register, never changes a frequency, and never programs a PLL or
 * voltage rail.  ioremap() refuses an unmappable region by returning NULL
 * rather than faulting, so a wrong address is a clean -EIO, not a hang.
 *
 * Build  (on the buildroot that produced dsi120.ko, the machine holding
 *  /home/hhhbwc/linux-build/linux-4.19 -- see dsi120/setup_buildroot.sh):
 *
 *   bash build.sh
 *
 * Required vermagic:
 *   4.19.81-perf+ SMP preempt mod_unload modversions aarch64
 *
 * Load (device, Magisk root, reuse the existing load_module helper):
 *   adb push epss_lut_probe.ko /data/local/tmp/
 *   adb shell su -Z u:r:magisk:s0 -c '/data/local/tmp/load_module /data/local/tmp/epss_lut_probe.ko 0'
 *   adb shell dmesg | grep epss_lut
 *   adb shell su -Z u:r:magisk:s0 -c 'rmmod epss_lut_probe'
 *
 * Optional module params to override the clock autodetect:
 *   epss_lut_probe.xo_rate_hz=19200000
 *   epss_lut_probe.alternate_rate_hz=0
 *
 * License: GPL v2, matching the kernel and the in-tree cpufreq driver.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/clk.h>

#define LUT_ROW_STRIDE 4      /* words per row, from qcom,lut-row-size = <4> */
#define ROW_WORD_LEN   (LUT_ROW_STRIDE * 4)
#define MAX_ROWS       128    /* kernel lut_max_entries is 32; 128 is generous */

struct epss_domain {
	const char *name;
	u64	base;
	const char *cpus;
};

static const struct epss_domain domains[] = {
	{ "freq-domain0 (small)",  0x18591000ULL, "CPU0-3" },
	{ "freq-domain1 (big)",    0x18592000ULL, "CPU4-6" },
	{ "freq-domain2 (prime)",  0x18593000ULL, "CPU7"   },
};
#define N_DOMAINS (sizeof(domains) / sizeof(domains[0]))

/* NOTE: 4.19's module_param() type-check macro only accepts basic C types,
 * not kernel typedefs.  u64 therefore fails with "expected ')' before '&'".
 * 4.19's moduleparam.h ships param_ops_ullong / param_check_ullong
 * (there is no 'ull' type), and the variable must be the matching C
 * type: unsigned long long. */
static unsigned long long xo_rate_hz;
module_param(xo_rate_hz, ullong, 0644);
MODULE_PARM_DESC(xo_rate_hz, "xo clock in Hz; 0 = autodetect via clk_get_rate");

static unsigned long long alternate_rate_hz;
module_param(alternate_rate_hz, ullong, 0644);
MODULE_PARM_DESC(alternate_rate_hz, "alternate clock in Hz; 0 = autodetect");

static u64 row_freq_hz(u32 fw)
{
	u32 src = (fw >> 30) & 0x3;
	u32 lval = fw & 0xff;

	if (src == 0)
		return alternate_rate_hz ? alternate_rate_hz / 2 : 0;
	return xo_rate_hz ? xo_rate_hz * lval / 1000 : 0;
}

/* The probe at 0xffffff9021bb4a5c-0xffffff9021bb4a8c computes
 *   freq_kHz = round( clk_hz * LVAL / 8000 )
 * via  x10 = clk * LVAL;  x10 >>= 3;  x10 = umulh(x10, 0x20c49ba5e353f7cf) >> 4
 * (0x20c49ba5e353f7cf is the standard /1000 magic constant).
 * The /8 then /1000 = /8000.
 *
 * The DTB xo-board node gives clock-frequency = 0x0249f000 = 38,400,000 Hz.
 * 38.4 MHz alone cannot reach 2841600 kHz at LVAL 148, so the effective
 * clock is xo x 4 = 153.6 MHz:
 *   153600000 * 148 / 8000 = 2841600 kHz   (exact match to sysfs)
 *   153600000 * 126 / 8000 = 2419200 kHz   (exact match, CPU4-6)
 *   153600000 *  94 / 8000 = 1804800 kHz   (exact match, CPU0-3)
 * All three domains share one clock.  This is derived, not measured. */
#define EPSS_XO_HZ  38400000ULL
#define EPSS_CLK_HZ (4 * EPSS_XO_HZ)
#define EPSS_DIV    8000ULL

static u32 row_freq_khz(u32 lval)
{
	return (u32)(EPSS_CLK_HZ * lval / EPSS_DIV / 1000);
}

static u32 lval_for_khz(u32 khz)
{
	return (u32)(khz * EPSS_DIV / EPSS_CLK_HZ);
}

static int dump_domain(int idx)
{
	const struct epss_domain *d = &domains[idx];
	void __iomem *reg;
	void __iomem *freq;
	void __iomem *volt;
	u32 prev = 0;
	int saw_first = 0;
	int i;

	reg = ioremap(d->base, 0x400);
	if (!reg) {
		pr_err("epss_lut: ioremap(0x%llx) failed - domain %d unreadable\n",
		       (unsigned long long)d->base, idx);
		return -EIO;
	}
	freq = reg + 0x100;
	volt = reg + 0x200;

	pr_info("epss_lut: === %s @ 0x%llx (%s) ===\n",
		d->name, (unsigned long long)d->base, d->cpus);
	pr_info("epss_lut:   reg[0x000] enable/ctrl = 0x%08x\n", readl(reg));
	pr_info("epss_lut:   reg[0x320] perf-state  = 0x%08x  (the only driver write target)\n",
		readl(reg + 0x320));
	for (i = 0x0F8; i < 0x104; i += 4)
		pr_info("epss_lut:   reg[0x%03x] = 0x%08x\n", i, readl(reg + i));

	/* Sweep the whole 0x1000 region above the perf-state register for any
	 * non-zero word, so nothing parked outside the assumed LUT window is
	 * missed.  Read-only. */
	{
		int nz = 0, shown = 0;
		for (i = 0x300; i < 0x1000; i += 4) {
			u32 w = readl(reg + i);
			if (w) {
				nz++;
				if (shown++ < 40)
					pr_info("epss_lut:   reg[0x%03x] = 0x%08x (above LUT)\n", i, w);
			}
		}
		if (nz)
			pr_info("epss_lut:   %d non-zero word(s) at/above 0x300 (first %d shown)\n", nz, shown);
		else
			pr_info("epss_lut:   nothing non-zero at/above 0x300 -> nothing hidden past perf-state\n");
	}

	/* The driver never touches 0x34c or 0x3c0, but they differ per domain:
	 *   0x34c: small=0x7758  big=0x9490  prime=0xccc8
	 *   0x3c0: small=0x4040  big=0x8000  prime=0x8000
	 * prime/big = 2x small on 0x3c0, and prime = 1.36x small on 0x34c.
	 * These look like hardware frequency/voltage limit clamps.  Dump them
	 * with field breakdowns so the encoding is visible.  READ-ONLY. */
	{
		u32 c0 = readl(reg + 0x3c0);
		u32 c4 = readl(reg + 0x3c4);
		u32 c8 = readl(reg + 0x3c8);
		u32 cc = readl(reg + 0x3cc);
		u32 c34c = readl(reg + 0x34c);
		u32 c348 = readl(reg + 0x348);
		u32 c350 = readl(reg + 0x350);
		u32 c354 = readl(reg + 0x354);
		u32 c304 = readl(reg + 0x304);
		u32 c308 = readl(reg + 0x308);

		pr_info("epss_lut:   === DCVSH limit registers (driver never reads these) ===\n");
		pr_info("epss_lut:   0x304=0x%08x  0x308=0x%08x\n", c304, c308);
		pr_info("epss_lut:   0x348=0x%08x  0x34c=0x%08x  0x350=0x%08x  0x354=0x%08x\n",
			c348, c34c, c350, c354);
		pr_info("epss_lut:   0x3c0=0x%08x  0x3c4=0x%08x  0x3c8=0x%08x  0x3cc=0x%08x\n",
			c0, c4, c8, cc);

		pr_info("epss_lut:   0x3c0 decode: 0x%08x -> bit15=%d, [14:0]=0x%04x\n",
			c0, (c0 >> 15) & 1, c0 & 0x7fff);
		pr_info("epss_lut:   0x34c decode: 0x%08x -> if kHz: %u kHz = %u.%03u MHz\n",
			c34c, c34c, c34c / 1000, c34c % 1000);
		pr_info("epss_lut:   0x34c decode: 0x%08x -> if raw count: %u\n", c34c, c34c);

		/* 0x3c0..0x3d4 looked like a pointer/hash block in earlier runs
		 * (values change between runs).  Check if it is stable now. */
		pr_info("epss_lut:   0x3d0=0x%08x  0x3d4=0x%08x\n",
			readl(reg + 0x3d0), readl(reg + 0x3d4));
	}

	pr_info("epss_lut:   row SRC CNT LVAL  freq_kHz   F-row (word0..3)                    V-row (word0..3)                    volt_uV\n");

	for (i = 0; i < MAX_ROWS; i++) {
		u32 fw = readl(freq + i * ROW_WORD_LEN);
		u32 f1 = readl(freq + i * ROW_WORD_LEN + 4);
		u32 f2 = readl(freq + i * ROW_WORD_LEN + 8);
		u32 f3 = readl(freq + i * ROW_WORD_LEN + 12);
		u32 vw = readl(volt + i * ROW_WORD_LEN);
		u32 v1 = readl(volt + i * ROW_WORD_LEN + 4);
		u32 v2 = readl(volt + i * ROW_WORD_LEN + 8);
		u32 v3 = readl(volt + i * ROW_WORD_LEN + 12);
		u32 src = (fw >> 30) & 0x3;
		u32 cnt = (fw >> 16) & 0x7;
		u32 volt = vw & 0xfff;
		u32 khz = row_freq_khz(fw & 0xff);

		if (!fw && !saw_first) {
			pr_info("epss_lut:   %2d: all-zero row -> no entries\n", i);
			break;
		}
		/* Full 4-word freq row + full 4-word volt row.  The driver
		 * only decodes word0 of each, so words 1-3 of the freq row and
		 * all of the volt row are undecoded - dump them raw so nothing
		 * is lost.  These are READ-ONLY accesses. */
		pr_info("epss_lut:   %2d S%d C%d L%3u %8u  F:0x%08x 0x%08x 0x%08x 0x%08x  V:0x%08x 0x%08x 0x%08x 0x%08x  %7u\n",
			i, src, cnt, fw & 0xff, khz,
			fw, f1, f2, f3,
			vw, v1, v2, v3,
			(unsigned)(volt * 1000));

		if (saw_first && fw == prev) {
			pr_info("epss_lut:   %2d: duplicate freq row -> boost/end marker, stop\n", i);
			break;
		}
		prev = fw;
		saw_first = 1;
		udelay(2);
	}
	pr_info("epss_lut: === domain %d done ===\n", idx);
	iounmap(reg);
	return 0;
}

static int probe_all(void)
{
	int i;
	int rc = 0;

	pr_info("epss_lut: ==== EPSS/DCVSH LUT read-only probe (SM8250 Kona) ====\n");
	pr_info("epss_lut: xo_rate=%llu Hz, alternate_rate=%llu Hz (0 = unresolved, freq shown as 0)\n",
		(unsigned long long)xo_rate_hz, (unsigned long long)alternate_rate_hz);
	pr_info("epss_lut: stride=%d words; SRC=31:30 CNT=18:16 LVAL=7:0; VOLT=11:0 (x1000 uV)\n",
		LUT_ROW_STRIDE);
	for (i = 0; i < N_DOMAINS; i++)
		if (dump_domain(i))
			rc = -EIO;
	pr_info("epss_lut: ==== probe complete, rc=%d ====\n", rc);
	return rc;
}

static int probe_thread(void *unused)
{
	set_user_nice(current, 19);
	return probe_all();
}

static void try_clk(const char *name, u64 *out, const char *label)
{
	struct clk *c = clk_get(NULL, name);

	if (!c || IS_ERR(c)) {
		pr_info("epss_lut: clk_get(%s) -> %s\n", name,
			!c ? "NULL" : "ERR");
		return;
	}
	*out = clk_get_rate(c);
	pr_info("epss_lut: %s <- clk %s = %llu Hz\n",
		label, name, (unsigned long long)*out);
	clk_put(c);
}

static int __init epss_lut_probe_init(void)
{
	struct task_struct *t;

	pr_info("epss_lut: loading (read-only)\n");

	/* Read-only clock accessors; clk_get_rate never reprograms anything.
	 * The DTB names the two clocks "xo" and "alternate", but a bare
	 * clk_get(NULL, name) needs the provider to be registered under that
	 * exact name, which it is not on this vendor build - so probe a set
	 * of candidates.  Whatever resolves is all we need: it is only used
	 * to turn LVAL into kHz for display, the raw LUT words are the data. */
	if (!xo_rate_hz)
		try_clk("xo", &xo_rate_hz, "xo_rate");
	if (!xo_rate_hz)
		try_clk("xo_clk", &xo_rate_hz, "xo_rate");
	if (!xo_rate_hz)
		try_clk("qcom,xo", &xo_rate_hz, "xo_rate");
	if (!xo_rate_hz)
		try_clk("qcom,tz,XO", &xo_rate_hz, "xo_rate");
	if (!xo_rate_hz)
		try_clk("rpmhcc_xo", &xo_rate_hz, "xo_rate");

	if (!alternate_rate_hz)
		try_clk("alternate", &alternate_rate_hz, "alternate_rate");
	if (!alternate_rate_hz)
		try_clk("qcom,alternate", &alternate_rate_hz, "alternate_rate");
	if (!alternate_rate_hz)
		try_clk("cpu_hw", &alternate_rate_hz, "alternate_rate");

	if (!xo_rate_hz && !alternate_rate_hz)
		pr_warn("epss_lut: no clock resolved; freq_kHz shown as 0, "
			"raw LVAL/VOLT words are still valid\n");

	t = kthread_run(probe_thread, NULL, "epss_lut_probe");
	if (IS_ERR(t)) {
		pr_err("epss_lut: kthread_run failed %ld\n", PTR_ERR(t));
		return PTR_ERR(t);
	}
	/* Let the dump land in dmesg before init returns. */
	schedule_timeout(msecs_to_jiffies(800));
	return 0;
}

static void __exit epss_lut_probe_exit(void)
{
	pr_info("epss_lut: unloading - no writes were performed\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PICO 4 CPU OC investigation");
MODULE_DESCRIPTION("Read-only EPSS/DCVSH freq/volt LUT dump for SM8250");
MODULE_VERSION("1.0");

module_init(epss_lut_probe_init);
module_exit(epss_lut_probe_exit);
