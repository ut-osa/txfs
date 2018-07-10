/*
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 *
 */
#include <asm/apic.h>
#include <asm/nmi.h>

#include <linux/cpumask.h>
#include <linux/kdebug.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/module.h>
#include <linux/delay.h>

#ifdef CONFIG_HARDLOCKUP_DETECTOR
u64 hw_nmi_get_sample_period(int watchdog_thresh)
{
	return (u64)(cpu_khz) * 1000 * watchdog_thresh;
}
#endif

#ifdef arch_trigger_all_cpu_backtrace
/* For reliability, we're prepared to waste bits here. */
static DECLARE_BITMAP(backtrace_mask, NR_CPUS) __read_mostly;

/* "in progress" flag of arch_trigger_all_cpu_backtrace */
static unsigned long backtrace_flag;

void arch_trigger_all_cpu_backtrace(bool include_self)
{
	int i;
	int cpu = get_cpu();

	if (test_and_set_bit(0, &backtrace_flag)) {
		/*
		 * If there is already a trigger_all_cpu_backtrace() in progress
		 * (backtrace_flag == 1), don't output double cpu dump infos.
		 */
		put_cpu();
		return;
	}

	cpumask_copy(to_cpumask(backtrace_mask), cpu_online_mask);
	if (!include_self)
		cpumask_clear_cpu(cpu, to_cpumask(backtrace_mask));

	if (!cpumask_empty(to_cpumask(backtrace_mask))) {
		pr_info("sending NMI to %s CPUs:\n",
			(include_self ? "all" : "other"));
		apic->send_IPI_mask(to_cpumask(backtrace_mask), NMI_VECTOR);
	}

	/* Wait for up to 10 seconds for all CPUs to do the backtrace */
	for (i = 0; i < 10 * 1000; i++) {
		if (cpumask_empty(to_cpumask(backtrace_mask)))
			break;
		mdelay(1);
		touch_softlockup_watchdog();
	}

	clear_bit(0, &backtrace_flag);
	smp_mb__after_atomic();
	put_cpu();
}

static int
arch_trigger_all_cpu_backtrace_handler(unsigned int cmd, struct pt_regs *regs)
{
	int cpu;

	cpu = smp_processor_id();

	if (cpumask_test_cpu(cpu, to_cpumask(backtrace_mask))) {
		static arch_spinlock_t lock = __ARCH_SPIN_LOCK_UNLOCKED;

		arch_spin_lock(&lock);
		printk(KERN_WARNING "NMI backtrace for cpu %d\n", cpu);
		show_regs(regs);
		arch_spin_unlock(&lock);
		cpumask_clear_cpu(cpu, to_cpumask(backtrace_mask));
		return NMI_HANDLED;
	}

	return NMI_DONE;
}
NOKPROBE_SYMBOL(arch_trigger_all_cpu_backtrace_handler);

static int __init register_trigger_all_cpu_backtrace(void)
{
	register_nmi_handler(NMI_LOCAL, arch_trigger_all_cpu_backtrace_handler,
				0, "arch_bt");
	return 0;
}
early_initcall(register_trigger_all_cpu_backtrace);
#endif
