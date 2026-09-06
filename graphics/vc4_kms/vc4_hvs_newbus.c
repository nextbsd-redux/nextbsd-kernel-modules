/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * newbus shim for vc4_hvs -- the BCM2712 Hardware Video Scaler
 * (nextbsd-kernel-extensions#51).
 *
 * The HVS is the compositor: it reads framebuffers from memory, composites and
 * scales planes, and feeds pixels to the display. Firmware KMS asks the
 * VideoCore firmware to do this over the property mailbox, and on BCM2712 the
 * firmware refuses the plane calls (#48), which is why that driver attaches
 * and reports vblank but cannot put correct pixels on screen. Programming the
 * HVS directly is what fixes that.
 *
 * WHY A SHIM RATHER THAN A PORT
 *
 * vc4_hvs.c is vendored unmodified from raspberrypi/linux. It registers itself
 * as a Linux platform driver:
 *
 *	static int vc4_hvs_dev_probe(struct platform_device *pdev)
 *	{
 *		return component_add(&pdev->dev, &vc4_hvs_ops);
 *	}
 *
 * LinuxKPI has no platform bus -- platform_driver_register() is a stub
 * returning -ENXIO -- so nothing would ever call that. Rather than build a
 * platform bus, this attaches to the device-tree node as an ordinary newbus
 * driver and calls the vendored probe itself. That is the same arrangement
 * vc4_fkms_master.c already uses on hardware, and it keeps the vendored source
 * untouched.
 *
 * `vc4_hvs_driver` is non-static upstream, so its .probe and .remove_new are
 * reachable from here without editing the file. `vc4_hvs_ops` is static and is
 * never referenced directly -- going through the driver struct is what avoids
 * needing it.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/component.h>

/* Defined in the vendored vc4_hvs.c; non-static there. */
extern struct platform_driver vc4_hvs_driver;

struct vc4_hvs_newbus_softc {
	device_t		bsddev;
	struct platform_device	pdev;		/* what the Linux probe sees */
	struct device_node	node;		/* our FDT node */
	struct resource	       *irq_res;
	int			irq_rid;
};

/*
 * Matched from the vendored driver's own of_match_table rather than a copy, so
 * the two cannot disagree about which parts this supports. bcm2711 and bcm2835
 * are in that table as well; they are accepted here because the vendored code
 * handles them, not because they have been tested.
 */
static int
vc4_hvs_newbus_probe(device_t dev)
{
	const struct of_device_id *id;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (vc4_hvs_driver.driver.of_match_table == NULL)
		return (ENXIO);

	for (id = vc4_hvs_driver.driver.of_match_table;
	    id->compatible[0] != '\0'; id++) {
		if (!ofw_bus_is_compatible(dev, id->compatible))
			continue;
		device_set_desc(dev, "BCM2712 Hardware Video Scaler");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
vc4_hvs_newbus_attach(device_t dev)
{
	struct vc4_hvs_newbus_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->bsddev = dev;
	sc->node.node = (intptr_t)ofw_bus_get_node(dev);

	/*
	 * Build the struct device the vendored probe expects.
	 *
	 * Every field here was established the hard way in #176: devres_head
	 * and devres_lock because devm_* walks them, irqents because
	 * request_irq() links into it, dev_set_name() because drm_dev_init()
	 * passes dev_name(parent) to drmm_kstrdup() and a NULL faults. Missing
	 * any one of them is a panic at a different point in attach.
	 */
	sc->pdev.dev.driver = &vc4_hvs_driver.driver;
	sc->pdev.name = "vc4_hvs";
	sc->pdev.dev.bsddev = dev;
	sc->pdev.dev.of_node = &sc->node;
	sc->pdev.dev.parent = NULL;
	INIT_LIST_HEAD(&sc->pdev.dev.devres_head);
	spin_lock_init(&sc->pdev.dev.devres_lock);
	INIT_LIST_HEAD(&sc->pdev.dev.irqents);
	dev_set_name(&sc->pdev.dev, "vc4_hvs");
	dev_set_drvdata(&sc->pdev.dev, NULL);

	/*
	 * Read the IRQ number and release the resource again: the vendored
	 * driver allocates it itself through devm_request_irq(), and holding it
	 * here would make that allocation fail. Same arrangement as the fkms
	 * master, for the same reason.
	 */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->irq_res != NULL) {
		sc->pdev.dev.irq = rman_get_start(sc->irq_res);
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
		sc->irq_res = NULL;
	} else {
		sc->pdev.dev.irq = LINUX_IRQ_INVALID;
	}

	if (bootverbose)
		device_printf(dev, "hvs: node %#lx irq %u\n",
		    (long)sc->node.node, sc->pdev.dev.irq);

	/*
	 * Hand off to the vendored probe, which calls component_add(). The HVS
	 * does not come up here -- it binds when a master's match list is
	 * complete, which is the point of the component framework.
	 */
	if (vc4_hvs_driver.probe == NULL)
		return (ENXIO);
	error = vc4_hvs_driver.probe(&sc->pdev);
	if (error != 0) {
		device_printf(dev, "vc4_hvs probe failed: %d\n", error);
		return (ENXIO);
	}
	return (0);
}

static int
vc4_hvs_newbus_detach(device_t dev)
{
	struct vc4_hvs_newbus_softc *sc = device_get_softc(dev);

	if (vc4_hvs_driver.remove_new != NULL)
		vc4_hvs_driver.remove_new(&sc->pdev);
	return (0);
}

static device_method_t vc4_hvs_newbus_methods[] = {
	DEVMETHOD(device_probe,		vc4_hvs_newbus_probe),
	DEVMETHOD(device_attach,	vc4_hvs_newbus_attach),
	DEVMETHOD(device_detach,	vc4_hvs_newbus_detach),
	DEVMETHOD_END
};

static driver_t vc4_hvs_newbus_driver = {
	"vc4_hvs",
	vc4_hvs_newbus_methods,
	sizeof(struct vc4_hvs_newbus_softc),
};

/* The HVS hangs off simplebus on a Pi 5; ofwbus is registered for symmetry
 * with the fkms master, which needed both. */
DRIVER_MODULE(vc4_hvs, simplebus, vc4_hvs_newbus_driver, 0, 0);
DRIVER_MODULE(vc4_hvs_ofwbus, ofwbus, vc4_hvs_newbus_driver, 0, 0);
