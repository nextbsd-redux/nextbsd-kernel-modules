// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_drv.c
 * Copyright 2012 Red Hat Inc.
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */
#include <linux/module.h>
#include <linux/pci.h>
#ifndef __FreeBSD__
/*
 * vt_kern.h has no linuxkpi equivalent, and nothing in this file uses it: the
 * console handover that needed it on Linux is done here by
 * drm_aperture_remove_conflicting_pci_framebuffers(), which drm-kmod
 * implements against vt(4)/vt_drmfb directly.
 */
#include <linux/vt_kern.h>
#endif

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_ttm.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>

#include "vbox_drv.h"

#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/sysctl.h>

/*
 * The Makefile passes -DDRM_SYSCTL_PARAM_PREFIX=_vboxvideo, so linuxkpi's
 * moduleparam.h hangs this driver's tunables off hw.vboxvideo; that parent
 * node must be declared in the SAME translation unit the parameters expand
 * in, which is this file (it holds the module_param_named() below). Each
 * drm-kmod driver does the same in its own source; putting it in the
 * companion glue file leaves this TU without it and fails to link.
 */
SYSCTL_NODE(_hw, OID_AUTO, vboxvideo, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "vboxvideo parameters");
#endif /* __FreeBSD__ */

static int vbox_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, vbox_modeset, int, 0400);

static const struct drm_driver driver;

static const struct pci_device_id pciidlist[] = {
	{ PCI_DEVICE(0x80ee, 0xbeef) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static int vbox_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct vbox_private *vbox;
	int ret = 0;

	if (!vbox_check_supported(VBE_DISPI_ID_HGSMI))
		return -ENODEV;

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &driver);
	if (ret)
		return ret;

	vbox = devm_drm_dev_alloc(&pdev->dev, &driver,
				  struct vbox_private, ddev);
	if (IS_ERR(vbox))
		return PTR_ERR(vbox);

	pci_set_drvdata(pdev, vbox);
	mutex_init(&vbox->hw_mutex);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = vbox_hw_init(vbox);
	if (ret)
		return ret;

	ret = vbox_mm_init(vbox);
	if (ret)
		goto err_hw_fini;

	ret = vbox_mode_init(vbox);
	if (ret)
		goto err_hw_fini;

	ret = vbox_irq_init(vbox);
	if (ret)
		goto err_mode_fini;

	ret = drm_dev_register(&vbox->ddev, 0);
	if (ret)
		goto err_irq_fini;

	drm_fbdev_ttm_setup(&vbox->ddev, 32);

	return 0;

err_irq_fini:
	vbox_irq_fini(vbox);
err_mode_fini:
	vbox_mode_fini(vbox);
err_hw_fini:
	vbox_hw_fini(vbox);
	return ret;
}

static void vbox_pci_remove(struct pci_dev *pdev)
{
	struct vbox_private *vbox = pci_get_drvdata(pdev);

	drm_dev_unregister(&vbox->ddev);
	drm_atomic_helper_shutdown(&vbox->ddev);
	vbox_irq_fini(vbox);
	vbox_mode_fini(vbox);
	vbox_hw_fini(vbox);
}

static void vbox_pci_shutdown(struct pci_dev *pdev)
{
	struct vbox_private *vbox = pci_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&vbox->ddev);
}

static int vbox_pm_suspend(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);
	int error;

	error = drm_mode_config_helper_suspend(&vbox->ddev);
	if (error)
		return error;

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int vbox_pm_resume(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pci_enable_device(pdev))
		return -EIO;

	return drm_mode_config_helper_resume(&vbox->ddev);
}

static int vbox_pm_freeze(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&vbox->ddev);
}

static int vbox_pm_thaw(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(&vbox->ddev);
}

static int vbox_pm_poweroff(struct device *dev)
{
	struct vbox_private *vbox = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(&vbox->ddev);
}

static const struct dev_pm_ops vbox_pm_ops = {
	.suspend = vbox_pm_suspend,
	.resume = vbox_pm_resume,
	.freeze = vbox_pm_freeze,
	.thaw = vbox_pm_thaw,
	.poweroff = vbox_pm_poweroff,
	.restore = vbox_pm_resume,
};

static struct pci_driver vbox_pci_driver = {
#ifdef __linux__
	.name = DRIVER_NAME,
#elif defined(__FreeBSD__)
	/*
	 * LinuxKPI routes registration by driver NAME:
	 *
	 *     pdrv->isdrm = strcmp(pdrv->name, "drmn") == 0;
	 *     dc = pdrv->isdrm ? devclass_create("vgapci")
	 *                      : devclass_find("pci");
	 *
	 * (sys/compat/linuxkpi/common/src/linux_pci.c). A DRM driver has to
	 * attach under vgapci -- VirtualBox's 80ee:beef is the guest's boot
	 * video device -- so anything not called "drmn" registers on the plain
	 * pci devclass and silently never matches: the module loads, sits
	 * resident, and no probe is ever attempted. bochs, i915 and radeon all
	 * carry this same override.
	 */
	.name = "drmn",	/* LinuxKPI expects this name to enable drm support */
#endif
	.id_table = pciidlist,
	.probe = vbox_pci_probe,
	.remove = vbox_pci_remove,
	.shutdown = vbox_pci_shutdown,
	.driver.pm = pm_sleep_ptr(&vbox_pm_ops),
};

DEFINE_DRM_GEM_FOPS(vbox_fops);

static const struct drm_driver driver = {
	.driver_features =
	    DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC | DRIVER_CURSOR_HOTSPOT,

	.fops = &vbox_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	DRM_GEM_VRAM_DRIVER,
};

#ifdef __FreeBSD__
/*
 * Register the way every drm-kmod driver does, for two reasons.
 *
 * 1. drm_module_pci_driver_if_modeset() expands to
 *        module_driver(drv, reg, unreg, __modeset)
 *    -- four arguments. Linux's module_driver() is variadic and swallows the
 *    fourth; FreeBSD's linuxkpi declares it with exactly three
 *    (linux/device/driver.h:17), so that macro cannot expand here at all.
 *
 * 2. More fundamentally, the whole drm_module_pci_driver() family only emits
 *    SYSINITs -- it never declares a FreeBSD module. Without DECLARE_MODULE
 *    the .ko carries dependency metadata for a module that does not exist,
 *    and kldload rejects the file outright with ENOEXEC ("Exec format
 *    error"). LKPI_DRIVER_MODULE is drm-kmod's answer: it builds the
 *    moduledata_t and DECLARE_MODULEs it, which is why i915, amdgpu, radeon
 *    and bochs all use it rather than the drm_module_* macros.
 *
 * drm_pci_{,un}register_driver_if_modeset() are the same helpers the Linux
 * macro would have called, so the modeset semantics are preserved exactly:
 * refuse when modeset==0, and when firmware-drivers-only is set with modeset
 * unspecified.
 */
static int __init
vbox_drm_init(void)
{
	return (drm_pci_register_driver_if_modeset(&vbox_pci_driver,
	    vbox_modeset));
}

static void __exit
vbox_drm_exit(void)
{
	drm_pci_unregister_driver_if_modeset(&vbox_pci_driver, vbox_modeset);
}

LKPI_DRIVER_MODULE(vboxvideo, vbox_drm_init, vbox_drm_exit);
#else
drm_module_pci_driver_if_modeset(vbox_pci_driver, vbox_modeset);
#endif

MODULE_AUTHOR("Oracle Corporation");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
