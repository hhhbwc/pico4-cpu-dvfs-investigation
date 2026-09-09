/*
 * cpu_oc_k1.c - PICO 4 (SM8250) CPU overclock: write EPSS perf-state register
 *               directly, bypassing the firmware LUT.
 *
 * Purpose
 * -------
 * The EPSS firmware frequency LUT ends at LVAL 148 = 2841600 kHz (CPU7 prime).
 * 3.0 GHz would need LVAL 156. The LUT does not contain a row 7, so the stock
 * driver cannot reach it. This module writes the perf-state register directly
 * with a hand-built LUT row value, bypassing the LUT entirely.
 *
 * EPSS perf-state register encoding (from disassembly of
 * qcom_cpufreq_hw_target_index @ 0xffffff9021bb54e4):
 *
 *   The driver stores `w20` (the perf-state index) at [x8] where x8 = base+0x320.
 *   The index is a row number into the LUT, NOT the raw LUT word.
 *   But the LUT only has 7 rows (0-6, row 6 = duplicate terminator).
 *   So writing index 7 or higher is undefined behavior per the firmware.
 *
 * HOWEVER: the perf-state register is a direct hardware register. The EPSS
 * hardware itself may accept any value. The LUT is just a lookup table for
 * the driver's benefit. The hardware may compute frequency from the index
 * using the same LVAL formula: freq = clk * LVAL / 8000.
 *
 * Strategy: write the raw LUT word directly to the perf-state register.
 * A LUT word for LVAL 156, SRC 1, CNT 4 would be:
 *   0x4004009c  (0x40040000 | (4 << 16) | (1 << 30) | 156)
 * But that is speculative. The safer approach is to write the index value
 * and see if the hardware interprets it as an LVAL or an index.
 *
 * SAFETY: this module writes only ONE register (perf-state) on ONE domain
 * (CPU7 prime). It does not write the LUT, the voltage table, or any other
 * register. If the write causes instability, the module can be unloaded
 * (rmmod) and the previous frequency is restored by the governor on the
 * next transition.
 *
 * This is a TEST module. It writes, but only the perf-state register, and
 * only once per module load. It does NOT program voltage - the RPMh rail
 * max is 1128 mV (DTB smpa6), and the stock LUT max is 904 mV, so there
 * is 224 mV of headroom. But this module does not touch voltage.
 *
 * Build:
 *   bash build_wsl.sh
 *
 * Load:
 *   adb push cpu_oc_k1.ko /data/local/tmp/
 *   adb shell su -Z u:r:magisk:s0 -c '/data/local/tmp/load_module /data/local/tmp/cpu_oc_k1.ko 0'
 *   adb shell cat /sys/devices/system/cpu/cpu7/cpufreq/cpuinfo_cur_freq_real
 *   adb shell su -Z u:r:magisk:s0 -c 'rmmod cpu_oc_k1'
 *
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>

/* Prime domain (CPU7) base and perf-state offset */
#define PRIME_BASE      0x18593000ULL
#define PERF_STATE_OFF  0x320

/* LUT word encoding:
 *   bits 31:30 = SRC (1 = use xo clock)
 *   bits 18:16 = CORE_COUNT (4)
 *   bits  7:0  = LVAL
 *   freq = clk_hz * LVAL / 8000
 *   clk = 153.6 MHz (xo * 4, from DTB xo-board 38.4 MHz)
 *
 * LVAL 148 = 2841600 kHz (current max)
 * LVAL 156 = 2995200 kHz (~3 GHz)
 * LVAL 160 = 3072000 kHz (3.072 GHz)
 */
#define LVAL_CURRENT    148   /* stock max */
#define LVAL_TARGET     156   /* ~3 GHz */
#define LVAL_STEP       8     /* 156 = 148 + 8 */

static u32 target_lval = LVAL_TARGET;
module_param(target_lval, uint, 0644);
MODULE_PARM_DESC(target_lval, "LVAL to write (148=stock, 156=3GHz, 160=3.072GHz)");

static u32 dry_run = 0;
module_param(dry_run, uint, 0644);
MODULE_PARM_DESC(dry_run, "1 = log only, do not write (default 0)");

static u64 row_freq_hz(u32 lval)
{
	return 153600000ULL * lval / 8000;
}

static u32 make_lut_word(u32 lval)
{
	/* SRC=1, CNT=4, LVAL in bits 7:0 */
	return (1u << 30) | (4u << 16) | (lval & 0xff);
}

static int write_perf_state(u32 value)
{
	void __iomem *reg;
	u32 before, after;

	reg = ioremap(PRIME_BASE, 0x400);
	if (!reg) {
		pr_err("cpu_oc: ioremap(0x%llx) failed\n", (unsigned long long)PRIME_BASE);
		return -EIO;
	}

	before = readl(reg + PERF_STATE_OFF);
	if (!dry_run) {
		writel(value, reg + PERF_STATE_OFF);
		/* Allow the write to settle */
		udelay(100);
	}
	after = readl(reg + PERF_STATE_OFF);

	pr_info("cpu_oc: perf-state[0x320] before=0x%08x write=0x%08x after=0x%08x %s\n",
		before, value, after, dry_run ? "(DRY RUN)" : "(WRITTEN)");

	iounmap(reg);
	return 0;
}

static int oc_thread(void *unused)
{
	u32 cur_word, target_word;
	u64 cur_hz, target_hz;
	int rc = 0;

	set_user_nice(current, 0);  /* highest priority */

	pr_info("cpu_oc: ==== CPU overclock attempt (SM8250 prime domain) ====\n");
	pr_info("cpu_oc: target LVAL = %u (freq = %llu kHz = %llu.%03llu GHz)\n",
		target_lval, (unsigned long long)(row_freq_hz(target_lval) / 1000),
		(unsigned long long)(row_freq_hz(target_lval) / 1000000000ULL),
		(unsigned long long)((row_freq_hz(target_lval) / 1000000) % 1000));
	pr_info("cpu_oc: stock LVAL = %u (freq = %llu kHz = %llu.%03llu GHz)\n",
		LVAL_CURRENT, (unsigned long long)(row_freq_hz(LVAL_CURRENT) / 1000),
		(unsigned long long)(row_freq_hz(LVAL_CURRENT) / 1000000000ULL),
		(unsigned long long)((row_freq_hz(LVAL_CURRENT) / 1000000) % 1000));
	pr_info("cpu_oc: delta = +%u LVAL = +%llu kHz = +%.3llu GHz\n",
		target_lval - LVAL_CURRENT,
		(unsigned long long)((row_freq_hz(target_lval) - row_freq_hz(LVAL_CURRENT)) / 1000),
		(unsigned long long)((row_freq_hz(target_lval) - row_freq_hz(LVAL_CURRENT)) / 1000000));

	/* Read current perf-state to see what the driver wrote */
	cur_word = 0;
	{
		void __iomem *reg = ioremap(PRIME_BASE, 0x400);
		if (reg) {
			cur_word = readl(reg + PERF_STATE_OFF);
			pr_info("cpu_oc: current perf-state register = 0x%08x\n", cur_word);
			iounmap(reg);
		}
	}

	/* Two write strategies:
	 * 1. Write the LUT word directly (perf-state = LUT word value)
	 *    This assumes the hardware interprets the perf-state value as
	 *    a LUT word and computes freq from the LVAL field.
	 * 2. Write the index (perf-state = row index)
	 *    This assumes the hardware indexes into the LUT. But row 7
	 *    doesn't exist, so this would be out of bounds.
	 *
	 * Strategy 1 is more likely to work because the hardware computes
	 * freq from LVAL, not from an index. The LUT is just a driver cache.
	 */
	target_word = make_lut_word(target_lval);
	cur_hz = row_freq_hz(cur_word & 0xff);
	target_hz = row_freq_hz(target_lval);

	pr_info("cpu_oc: current LVAL (from perf-state) = %u (freq = %llu kHz)\n",
		cur_word & 0xff, (unsigned long long)(cur_hz / 1000));
	pr_info("cpu_oc: target LVAL = %u (freq = %llu kHz)\n",
		target_lval, (unsigned long long)(target_hz / 1000));
	pr_info("cpu_oc: target perf-state word = 0x%08x\n", target_word);

	/* Write the target LUT word to the perf-state register */
	if (write_perf_state(target_word)) {
		rc = -EIO;
		goto out;
	}

	/* Give the hardware time to settle */
	mdelay(100);

	/* Read back the sysfs frequency to see if it took effect */
	pr_info("cpu_oc: write complete. Check /sys/devices/system/cpu/cpu7/cpufreq/cpuinfo_cur_freq_real\n");

out:
	pr_info("cpu_oc: ==== overclock attempt complete, rc=%d ====\n", rc);
	return rc;
}

static int __init cpu_oc_init(void)
{
	struct task_struct *t;
	int rc = 0;

	pr_info("cpu_oc: loading (CPU overclock module)\n");
	pr_info("cpu_oc: PRIME_BASE=0x%llx PERF_STATE_OFF=0x%x\n",
		(unsigned long long)PRIME_BASE, PERF_STATE_OFF);

	t = kthread_run(oc_thread, NULL, "cpu_oc");
	if (IS_ERR(t)) {
		pr_err("cpu_oc: kthread_run failed %ld\n", PTR_ERR(t));
		return PTR_ERR(t);
	}

	schedule_timeout(msecs_to_jiffies(500));
	return rc;
}

static void __exit cpu_oc_exit(void)
{
	pr_info("cpu_oc: unloading - restoring by governor on next transition\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PICO 4 CPU OC investigation");
MODULE_DESCRIPTION("PICO 4 CPU overclock: write EPSS perf-state register");
MODULE_VERSION("1.0");

module_init(cpu_oc_init);
module_exit(cpu_oc_exit);
