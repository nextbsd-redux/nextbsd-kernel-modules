/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * newbus shim for vc4_hdmi_driver (nextbsd-kernel-extensions#51).
 *
 * The HDMI controller and its PHY. A Pi 5 has two, hdmi0 and hdmi1, matched by
 * separate compatible strings with different .data.
 *
 * Note its driver declares .pm = &vc4_hdmi_pm_ops. LinuxKPI implements no
 * runtime PM, so those callbacks are never invoked; the block stays powered,
 * which is correct-but-wasteful rather than broken.
 *
 * All the work is in vc4_newbus.c; this is the DEVMETHOD table and the name.
 * vc4_hdmi_driver is non-static in the vendored source, so its .probe and
 * .remove_new are reachable without editing that file.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <linux/platform_device.h>

#include "vc4_newbus.h"

extern struct platform_driver vc4_hdmi_driver;

static int
vc4_hdmi_newbus_probe(device_t dev)
{

	return (vc4_newbus_probe(dev, &vc4_hdmi_driver, "BCM2712 HDMI controller"));
}

static int
vc4_hdmi_newbus_attach(device_t dev)
{

	return (vc4_newbus_attach(dev, &vc4_hdmi_driver, "vc4_hdmi"));
}

static int
vc4_hdmi_newbus_detach(device_t dev)
{

	return (vc4_newbus_detach(dev, &vc4_hdmi_driver));
}

static device_method_t vc4_hdmi_newbus_methods[] = {
	DEVMETHOD(device_probe,		vc4_hdmi_newbus_probe),
	DEVMETHOD(device_attach,	vc4_hdmi_newbus_attach),
	DEVMETHOD(device_detach,	vc4_hdmi_newbus_detach),
	DEVMETHOD_END
};

static driver_t vc4_hdmi_newbus_driver = {
	"vc4_hdmi",
	vc4_hdmi_newbus_methods,
	sizeof(struct vc4_newbus_softc),
};

DRIVER_MODULE(vc4_hdmi, simplebus, vc4_hdmi_newbus_driver, 0, 0);
DRIVER_MODULE(vc4_hdmi_ofwbus, ofwbus, vc4_hdmi_newbus_driver, 0, 0);
