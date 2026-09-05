/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linux/reset.h (nextbsd-kernel-extensions#51).
 *
 * Two symbols, used by vc4_hdmi.c to pulse the HDMI block's reset line on
 * parts that expose one.
 *
 * devm_reset_control_get() reports "no reset controller", so the driver skips
 * the reset. On BCM2712 the firmware has already brought the HDMI block out of
 * reset before the OS runs, which is why this is survivable here -- it is NOT
 * a general reset-controller implementation, and a part that genuinely needs
 * an explicit reset would come up in an undefined state with these stubs.
 */
#ifndef _LINUXKPI_LINUX_RESET_H_
#define	_LINUXKPI_LINUX_RESET_H_

#include <linux/err.h>
#include <linux/device.h>

struct reset_control;

static inline struct reset_control *
devm_reset_control_get(struct device *dev, const char *id)
{

	return (ERR_PTR(-ENOENT));
}

static inline struct reset_control *
devm_reset_control_get_optional(struct device *dev, const char *id)
{

	return (NULL);
}

static inline int
reset_control_reset(struct reset_control *rstc)
{

	if (rstc == NULL)
		return (0);
	return (-ENOSYS);
}

#endif /* _LINUXKPI_LINUX_RESET_H_ */
