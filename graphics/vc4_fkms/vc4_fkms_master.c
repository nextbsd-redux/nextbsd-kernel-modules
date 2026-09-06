// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The DRM master for vc4_firmware_kms, and the FreeBSD device that carries it
 * (nextbsd-kernel#176).
 *
 * In Linux this role belongs to vc4_drv.c, which brings up a drm_device and
 * then calls component_bind_all() so each sub-device -- HDMI0, HDMI1, the HVS,
 * V3D -- attaches to it. Firmware KMS has none of those: the VideoCore
 * firmware owns the display hardware and answers over the property mailbox, so
 * the only component is fkms itself. What is left of vc4_drv.c after removing
 * everything fkms does not use is small enough to write here rather than
 * vendor 1200 lines of vc4_kms.c and vc4_drv.c for the fraction that applies.
 *
 * Two devices, deliberately, even though there is one node:
 *
 *   vc4_fkms_master	holds the drm_device as its drvdata, which is what
 *			vc4_fkms_bind() reads out of its `master` argument
 *   platform_device	the component; fkms overwrites *its* drvdata with the
 *			CRTC list at the end of bind (vc4_firmware_kms.c:2036)
 *
 * Collapsing them into one device would have that write clobber the
 * drm_device pointer the master needs.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include "vc4_fkms_param.h"

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <linux/component.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "vc4_drv.h"

/*
 * linuxkpi's moduleparam.h expands a driver's tunables under a sysctl node it
 * does not itself declare, so vc4_firmware_kms.c:44 refers to
 * sysctl___hw_vc4_fkms without anything defining it. Declared here, once:
 * bochs records that having it in two translation units is a duplicate symbol
 * at link time rather than a warning.
 */
SYSCTL_NODE(_hw, OID_AUTO, vc4_fkms, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "VideoCore firmware KMS");

DEFINE_DRM_GEM_DMA_FOPS(vc4_fkms_fops);

/*
 * No render node and no ioctls beyond the KMS set: this driver does not
 * expose the V3D engine, only the display path the firmware already drives.
 * DRIVER_GEM is still required because X allocates its front buffer through
 * dumb_create(), which is what pulled in the DMA GEM helpers in the first
 * place (nextbsd-kernel-extensions#42).
 */
static const struct drm_driver vc4_fkms_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.name			= "vc4-fkms",
	.desc			= "Broadcom VC4 firmware KMS",
	.date			= "20260904",
	.major			= 1,
	.minor			= 0,
	.fops			= &vc4_fkms_fops,
	DRM_GEM_DMA_DRIVER_OPS,
};

static const struct drm_mode_config_funcs vc4_fkms_mode_funcs = {
	.fb_create	= drm_gem_fb_create,
	.atomic_check	= drm_atomic_helper_check,
	.atomic_commit	= drm_atomic_helper_commit,
};

/*
 * Attach tracing (nextbsd-kernel#176). The first hardware load reset the
 * machine with no dmesg output at all, which means it died before anything in
 * attach printed. Each step announces itself BEFORE doing the thing, so the
 * last line on the console names the call that killed it rather than the last
 * one that succeeded.
 *
 * device_printf goes to the console synchronously, which is what makes it
 * survive a panic where a deferred log would not -- a deferred log loses
 * exactly the line that matters. That property is why this stays in the tree
 * rather than being deleted once bring-up finished: the next person to port a
 * firmware-KMS revision gets the same bisect for free with boot -v.
 */
#define	FKMS_TRACE(dev, msg)						\
	do {								\
		if (bootverbose)					\
			device_printf((dev), "attach: " msg "\n");	\
	} while (0)

struct vc4_fkms_softc {
	device_t		bsddev;
	struct resource		*irq_res;
	int			irq_rid;
	struct platform_device	pdev;		/* the component */
	struct device		master;		/* holds the drm_device */
	struct device_node	node;		/* our FDT node */
	struct vc4_dev		*vc4;
	bool			bound;
};

static struct ofw_compat_data compat_data[] = {
	{ "raspberrypi,rpi-firmware-kms-2712",	1 },
	{ "raspberrypi,rpi-firmware-kms",	1 },
	{ NULL,					0 }
};

static int
vc4_fkms_probe_bsd(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "VideoCore firmware KMS");
	return (BUS_PROBE_DEFAULT);
}

static int
vc4_fkms_attach(device_t dev)
{
	struct vc4_fkms_softc *sc;
	struct drm_device *drm;
	struct vc4_dev *vc4;
	int error;

	sc = device_get_softc(dev);
	sc->bsddev = dev;
	FKMS_TRACE(dev, "start");

	/*
	 * The FDT node, in the shape a Linux platform driver expects to find
	 * on dev->of_node. fkms reads it to follow its "brcm,firmware"
	 * phandle to the mailbox.
	 */
	FKMS_TRACE(dev, "1 ofw_bus_get_node");
	sc->node.node = (intptr_t)ofw_bus_get_node(dev);
	if (bootverbose)
		device_printf(dev, "attach: node phandle %#lx\n",
		    (long)sc->node.node);

	/*
	 * Both struct devices are built by hand, and deliberately WITHOUT
	 * device_initialize(): linuxkpi's is not a field initialiser, it
	 * dereferences dev->class and calls device_add_child() to manufacture a
	 * newbus device that in our case already exists. virtio_gpu_drm hit
	 * exactly this and records it. Filling the fields that get read --
	 * bsddev, and the devres list anything devm_* walks -- is both
	 * necessary and sufficient.
	 */
	/*
	 * dev.driver is NOT set: struct platform_driver::driver is now a
	 * module-private type (it has to carry of_match_table, which the
	 * kernel's struct device_driver no longer does), so it is not a
	 * struct device_driver * and cannot be assigned here. Nothing reads
	 * dev->driver -- of_device_get_match_data() takes the match table from
	 * the registry below instead.
	 */
	sc->pdev.name = "vc4_firmware_kms";
	sc->pdev.dev.bsddev = dev;
	/*
	 * struct device has no of_node member any more -- kernel patch 0040
	 * added one and inserting it mid-struct shifted every later field,
	 * page-faulting i915kms on a Wyse 5070 (gershwin-desktop#49). The
	 * mapping is module-private now; dev_of_node() reads it back.
	 */
	lkpi_set_of_node(&sc->pdev.dev, &sc->node,
	    vc4_firmware_kms_driver.driver.of_match_table);
	sc->pdev.dev.parent = NULL;
	INIT_LIST_HEAD(&sc->pdev.dev.devres_head);
	spin_lock_init(&sc->pdev.dev.devres_lock);
	/*
	 * request_irq() ends with list_add(&irqe->links, &dev->irqents), and a
	 * zeroed list head has a NULL next -- so this is a panic the moment an
	 * interrupt is registered, which is only reachable once
	 * nextbsd-kernel#189 lets request_irq() get that far at all.
	 * linux_pci.c:460 does the same for a PCI device.
	 */
	INIT_LIST_HEAD(&sc->pdev.dev.irqents);
	/*
	 * dev_name() is kobject_name(), which returns kobj.name verbatim -- NULL
	 * on a struct device nobody named. Anything that formats or copies it
	 * then faults, and DRM device init does use the parent's name. Cheap to
	 * set, and it removes a whole class of failure that would look like a
	 * crash in DRM rather than a missing field here.
	 */
	dev_set_name(&sc->pdev.dev, "vc4_firmware_kms");
	dev_set_drvdata(&sc->pdev.dev, NULL);

	/*
	 * fkms asks for its interrupt with platform_get_irq(pdev, 0), which
	 * answers from dev->irq -- so allocating it is our job. The node has
	 * one, used for the firmware's vblank notification; without it the
	 * driver still binds and simply never reports vblank.
	 */
	FKMS_TRACE(dev, "2 alloc irq");
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->irq_res != NULL) {
		sc->pdev.dev.irq = rman_get_start(sc->irq_res);
		/*
		 * Read the number, then hand the resource back. fkms registers
		 * its own handler with devm_request_irq(), which allocates the
		 * same rid itself -- holding it here makes that allocation fail
		 * and the interrupt is silently lost.
		 */
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	} else {
		sc->pdev.dev.irq = LINUX_IRQ_INVALID;
	}

	/*
	 * The master. devm_drm_dev_alloc() ties the drm_device's lifetime to
	 * this struct device, and to_vc4_dev() finds vc4_dev back from it.
	 */
	if (bootverbose)
		device_printf(dev, "attach: irq %u\n", sc->pdev.dev.irq);
	FKMS_TRACE(dev, "3 dma_priv + devm_drm_dev_alloc");
	sc->master.bsddev = dev;
	INIT_LIST_HEAD(&sc->master.devres_head);
	spin_lock_init(&sc->master.devres_lock);
	INIT_LIST_HEAD(&sc->master.irqents);
	dev_set_name(&sc->master, "vc4_fkms");

	/*
	 * The DMA GEM helpers allocate against drm->dev, which drm_dev_init()
	 * sets to this device. Without dma_priv every allocator in
	 * linux_pci.c returns NULL on its dma_priv == NULL check, so
	 * DRM_IOCTL_MODE_CREATE_DUMB fails with ENOMEM for a 1.2MB buffer on a
	 * machine with 16GB free -- measured, and the reason X could not get
	 * past ScreenInit.
	 *
	 * Both masks are 32-bit, matching what vc4_drm_bind() sets on Linux
	 * (dev->coherent_dma_mask = DMA_BIT_MASK(32)). The display side of this
	 * SoC does not address above 4GB, and the mailbox interface carries
	 * buffer addresses in a single 32-bit word.
	 */
	error = linux_dma_priv_init(&sc->master, DMA_BIT_MASK(32),
	    DMA_BIT_MASK(32));
	if (error != 0) {
		device_printf(dev, "dma_priv init failed: %d\n", error);
		goto fail;
	}
	vc4 = devm_drm_dev_alloc(&sc->master, &vc4_fkms_drm_driver,
	    struct vc4_dev, base);
	if (IS_ERR(vc4)) {
		device_printf(dev, "drm_dev_alloc failed: %ld\n", PTR_ERR(vc4));
		error = ENOMEM;
		goto fail;
	}
	sc->vc4 = vc4;
	drm = &vc4->base;

	/*
	 * BCM2712. The generation matters to shared vc4 code paths; fkms
	 * itself keys off firmware_kms, which its bind sets.
	 */
	vc4->gen = VC4_GEN_6_C;
	vc4->dev = &sc->pdev.dev;
	dev_set_drvdata(&sc->master, drm);

	FKMS_TRACE(dev, "4 drmm_mode_config_init");
	error = drmm_mode_config_init(drm);
	if (error != 0) {
		device_printf(dev, "mode config init failed: %d\n", error);
		goto fail;
	}
	drm->mode_config.funcs = &vc4_fkms_mode_funcs;
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 7680;
	drm->mode_config.max_height = 7680;

	/* Registers the component; fkms's own probe does nothing else. */
	FKMS_TRACE(dev, "5 fkms probe (component_add)");
	error = vc4_firmware_kms_driver.probe(&sc->pdev);
	if (error != 0) {
		device_printf(dev, "fkms probe failed: %d\n", error);
		goto fail;
	}

	FKMS_TRACE(dev, "6 component_bind_all -> fkms bind -> FIRMWARE MAILBOX");
	error = component_bind_all(&sc->master, drm);
	if (error != 0) {
		device_printf(dev, "fkms bind failed: %d\n", error);
		goto fail;
	}
	sc->bound = true;

	FKMS_TRACE(dev, "7 drm_vblank_init");
	drm->vblank_disable_immediate = true;
	error = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (error != 0) {
		device_printf(dev, "vblank init failed: %d\n", error);
		goto fail;
	}

	FKMS_TRACE(dev, "8 drm_mode_config_reset");
	drm_mode_config_reset(drm);

	FKMS_TRACE(dev, "9 drm_dev_register");
	error = drm_dev_register(drm, 0);
	if (error != 0) {
		device_printf(dev, "drm_dev_register failed: %d\n", error);
		goto fail;
	}

	device_printf(dev, "registered, %d CRTC(s)\n",
	    drm->mode_config.num_crtc);
	return (0);

fail:
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
	return (error == 0 ? ENXIO : error);
}

static int
vc4_fkms_detach(device_t dev)
{
	struct vc4_fkms_softc *sc = device_get_softc(dev);

	/*
	 * Unwind in reverse. sc->vc4 is NULL if attach failed before the
	 * drm_device existed, and component_unbind_all() would dereference it
	 * for the master data -- so both uses are guarded on the same check
	 * rather than only the first.
	 */
	if (sc->vc4 != NULL) {
		drm_dev_unregister(&sc->vc4->base);
		if (sc->bound)
			component_unbind_all(&sc->master, &sc->vc4->base);
	}
	vc4_firmware_kms_driver.remove(&sc->pdev);
	linux_dma_priv_uninit(&sc->master);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
	return (0);
}

static device_method_t vc4_fkms_methods[] = {
	DEVMETHOD(device_probe,		vc4_fkms_probe_bsd),
	DEVMETHOD(device_attach,	vc4_fkms_attach),
	DEVMETHOD(device_detach,	vc4_fkms_detach),
	DEVMETHOD_END
};

static driver_t vc4_fkms_driver_bsd = {
	"vc4_fkms",
	vc4_fkms_methods,
	sizeof(struct vc4_fkms_softc)
};

DRIVER_MODULE(vc4_fkms, simplebus, vc4_fkms_driver_bsd, 0, 0);
DRIVER_MODULE(vc4_fkms_ofwbus, ofwbus, vc4_fkms_driver_bsd, 0, 0);
MODULE_VERSION(vc4_fkms, 1);
MODULE_DEPEND(vc4_fkms, drmn, 2, 2, 2);
MODULE_DEPEND(vc4_fkms, drm_dma_helpers, 1, 1, 1);
/*
 * drm_extra_helpers carries drm_gem_fb_create(), which this module's
 * mode_config funcs point at. drm-kmod does not ship it, so it is not reachable
 * through drmn -- and MODULE_DEPEND is depth-1, so having it loaded is not
 * enough: the kernel linker only searches a module's declared dependencies.
 *
 * Measured on hardware rather than guessed. Without this edge, kextload fails
 * with "link_elf: symbol drm_gem_fb_create undefined" even after
 * IOGraphicsExtras is loaded and exporting it.
 */
MODULE_DEPEND(vc4_fkms, drm_extra_helpers, 1, 1, 1);
MODULE_DEPEND(vc4_fkms, linuxkpi, 1, 1, 1);
