/*
 * Idle functions for s390.
 *
 * Copyright IBM Corp. 2014
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/kprobes.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <asm/cputime.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include "entry.h"

static DEFINE_PER_CPU(struct s390_idle_data, s390_idle);

void __kprobes enabled_wait(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	unsigned long long idle_time;
	unsigned long psw_mask;

	trace_hardirqs_on();

	/* Wait for external, I/O or machine check interrupt. */
	psw_mask = PSW_KERNEL_BITS | PSW_MASK_WAIT | PSW_MASK_DAT |
		PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
	clear_cpu_flag(CIF_NOHZ_DELAY);

	/* Call the assembler magic in entry.S */
	psw_idle(idle, psw_mask);

	/* Account time spent with enabled wait psw loaded as idle time. */
	idle->sequence++;
	smp_wmb();
	idle_time = idle->clock_idle_exit - idle->clock_idle_enter;
	idle->clock_idle_enter = idle->clock_idle_exit = 0ULL;
	idle->idle_time += idle_time;
	idle->idle_count++;
	account_idle_time(idle_time);
	smp_wmb();
	idle->sequence++;
}

static ssize_t show_idle_count(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);
	unsigned long long idle_count;
	unsigned int sequence;

	do {
		sequence = ACCESS_ONCE(idle->sequence);
		idle_count = ACCESS_ONCE(idle->idle_count);
		if (ACCESS_ONCE(idle->clock_idle_enter))
			idle_count++;
	} while ((sequence & 1) || (ACCESS_ONCE(idle->sequence) != sequence));
	return sprintf(buf, "%llu\n", idle_count);
}
DEVICE_ATTR(idle_count, 0444, show_idle_count, NULL);

static ssize_t show_idle_time(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);
	unsigned long long now, idle_time, idle_enter, idle_exit;
	unsigned int sequence;

	do {
		now = get_tod_clock();
		sequence = ACCESS_ONCE(idle->sequence);
		idle_time = ACCESS_ONCE(idle->idle_time);
		idle_enter = ACCESS_ONCE(idle->clock_idle_enter);
		idle_exit = ACCESS_ONCE(idle->clock_idle_exit);
	} while ((sequence & 1) || (ACCESS_ONCE(idle->sequence) != sequence));
	idle_time += idle_enter ? ((idle_exit ? : now) - idle_enter) : 0;
	return sprintf(buf, "%llu\n", idle_time >> 12);
}
DEVICE_ATTR(idle_time_us, 0444, show_idle_time, NULL);

cputime64_t arch_cpu_idle_time(int cpu)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, cpu);
	unsigned long long now, idle_enter, idle_exit;
	unsigned int sequence;

	do {
		now = get_tod_clock();
		sequence = ACCESS_ONCE(idle->sequence);
		idle_enter = ACCESS_ONCE(idle->clock_idle_enter);
		idle_exit = ACCESS_ONCE(idle->clock_idle_exit);
	} while ((sequence & 1) || (ACCESS_ONCE(idle->sequence) != sequence));
	return idle_enter ? ((idle_exit ?: now) - idle_enter) : 0;
}

void arch_cpu_idle_enter(void)
{
	local_mcck_disable();
}

void arch_cpu_idle(void)
{
	if (!test_cpu_flag(CIF_MCCK_PENDING))
		/* Halt the cpu and keep track of cpu time accounting. */
		enabled_wait();
	local_irq_enable();
}

void arch_cpu_idle_exit(void)
{
	local_mcck_enable();
	if (test_cpu_flag(CIF_MCCK_PENDING))
		s390_handle_mcck();
}

void arch_cpu_idle_dead(void)
{
	cpu_die();
}
