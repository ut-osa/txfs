/*
 * Generic definitions for Marvell Armada_370_XP SoCs
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_ARMADA_370_XP_H
#define __MACH_ARMADA_370_XP_H

#ifdef CONFIG_SMP
#include <linux/cpumask.h>

#define ARMADA_XP_MAX_CPUS 4

void armada_xp_secondary_startup(void);
extern struct smp_operations armada_xp_smp_ops;
#endif

int armada_370_xp_pmsu_idle_enter(unsigned long deepidle);

#endif /* __MACH_ARMADA_370_XP_H */
