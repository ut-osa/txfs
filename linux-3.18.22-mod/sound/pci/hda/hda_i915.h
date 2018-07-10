/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#ifndef __SOUND_HDA_I915_H
#define __SOUND_HDA_I915_H

#ifdef CONFIG_SND_HDA_I915
int hda_display_power(bool enable);
void haswell_set_bclk(struct azx *chip);
int hda_i915_init(void);
int hda_i915_exit(void);
#else
static inline int hda_display_power(bool enable) { return 0; }
static inline void haswell_set_bclk(struct azx *chip) { return; }
static inline int hda_i915_init(void)
{
	return -ENODEV;
}
static inline int hda_i915_exit(void)
{
	return 0;
}
#endif

#endif
