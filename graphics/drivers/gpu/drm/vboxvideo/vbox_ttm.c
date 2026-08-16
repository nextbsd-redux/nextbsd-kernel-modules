// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_ttm.c
 * Copyright 2012 Red Hat Inc.
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com>
 */
#include <linux/pci.h>
#include <drm/drm_file.h>
#include "vbox_drv.h"

#ifdef __FreeBSD__
/*
 * linuxkpi has arch_phys_wc_add()/arch_phys_wc_del() (linux/io.h) but not the
 * devres wrapper, so build it from the two pieces it does have. Written to
 * match Linux' devm_arch_phys_wc_add() semantics exactly, including the
 * negative-return case (MTRRs exhausted or unsupported), which the sole caller
 * below deliberately ignores -- a missing write-combining range costs
 * framebuffer throughput, not correctness.
 */
static void
vbox_arch_phys_wc_release(void *arg)
{
	arch_phys_wc_del((int)(intptr_t)arg);
}

static inline int
devm_arch_phys_wc_add(struct device *dev, unsigned long base,
    unsigned long size)
{
	int mtrr;

	mtrr = arch_phys_wc_add(base, size);
	if (mtrr < 0)
		return (mtrr);
	if (devm_add_action_or_reset(dev, vbox_arch_phys_wc_release,
	    (void *)(intptr_t)mtrr) != 0)
		return (-ENOMEM);
	return (mtrr);
}
#endif /* __FreeBSD__ */

int vbox_mm_init(struct vbox_private *vbox)
{
	int ret;
	resource_size_t base, size;
	struct drm_device *dev = &vbox->ddev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	base = pci_resource_start(pdev, 0);
	size = pci_resource_len(pdev, 0);

	/* Don't fail on errors, but performance might be reduced. */
	devm_arch_phys_wc_add(&pdev->dev, base, size);

	ret = drmm_vram_helper_init(dev, base, vbox->available_vram_size);
	if (ret) {
		DRM_ERROR("Error initializing VRAM MM; %d\n", ret);
		return ret;
	}

	return 0;
}
