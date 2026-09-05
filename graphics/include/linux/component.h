/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linux/component.h -- the two entry points vc4_firmware_kms.c uses
 * (nextbsd-kernel#176).
 *
 * Linux's component framework exists to defer probe until every sub-device of
 * a multi-part device has registered, then bind them as one DRM device. vc4
 * needs that because HDMI0, HDMI1, the HVS and the V3D block are separate
 * platform devices.
 *
 * Firmware KMS does not have that problem: the firmware owns the display
 * hardware and presents it over one mailbox, so there is a single device and
 * nothing to wait for. Measured on the source -- component_add and
 * component_del, one call each, in bind and unbind. No component_master_*, no
 * match arrays, no aggregate driver.
 *
 * Declarations only for now. This is a compile probe; whether these end up as
 * real registration or as no-ops that call the ops directly is a decision for
 * the port, and one the probe is meant to inform.
 */
#ifndef _LINUXKPI_LINUX_COMPONENT_H_
#define	_LINUXKPI_LINUX_COMPONENT_H_

#include <linux/device.h>

struct component_ops {
	int	(*bind)(struct device *comp, struct device *master,
		    void *master_data);
	void	(*unbind)(struct device *comp, struct device *master,
		    void *master_data);
};

int	component_add(struct device *dev, const struct component_ops *ops);
void	component_del(struct device *dev, const struct component_ops *ops);

#endif /* _LINUXKPI_LINUX_COMPONENT_H_ */
