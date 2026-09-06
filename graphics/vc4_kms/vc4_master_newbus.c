/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * newbus master for the full vc4 KMS pipeline
 * (nextbsd-kernel-extensions#51).
 *
 * Attaches to the top-level vc4 device-tree node, builds the struct device the
 * vendored master expects, and hands off to vc4_platform_drm_probe(), which
 * registers vc4_drm_ops as a component master. When every component has
 * registered -- HVS, CRTC, HDMI, each via its own newbus shim -- the master's
 * bind runs and brings up the drm_device.
 *
 * This is the same arrangement vc4_fkms_master.c uses for firmware KMS, and
 * the reason neither needs LinuxKPI's platform bus.
 *
 * BINDING ORDER
 *
 * The vendored source documents constraints:
 *
 *	The TXP driver needs to be bound before the PixelValves (CRTC) but
 *	after the HVS to set the possible_crtc field properly. The HDMI driver
 *	needs to be bound after the HVS so that we can lookup the HVS maximum
 *	core clock rate and figure out if we support 4kp60 or not.
 *
 * Upstream gets that ordering from component_drivers[], because
 * vc4_match_add_drivers() walks that array and the match list preserves its
 * order. Our components instead register as their newbus drivers attach, which
 * follows device-tree order -- and nothing guarantees the tree lists the HVS
 * before the HDMI.
 *
 * This is a REAL RISK and is not yet handled. If HDMI binds before HVS the
 * clock-rate lookup reads an unpopulated HVS and 4kp60 support is decided
 * wrongly -- a silent misconfiguration, not a failure. Verifying the actual
 * bind order on hardware is the first thing to do once this attaches; if it is
 * wrong, the fix is for component_bind_all() to bind in match-list order
 * rather than registration order, which is what upstream effectively does.
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

#include "vc4_newbus.h"

/* Non-static in the vendored vc4_drv.c. */
extern struct platform_driver vc4_platform_driver;

static int
vc4_master_newbus_probe(device_t dev)
{

	return (vc4_newbus_probe(dev, &vc4_platform_driver,
	    "Broadcom VideoCore VI (KMS)"));
}

static int
vc4_master_newbus_attach(device_t dev)
{
	int error;

	/*
	 * Same struct device construction as every component, then the
	 * vendored probe -- which calls component_master_add_with_match()
	 * rather than binding anything itself.
	 *
	 * A master whose components have not all registered yet is NOT an
	 * error: component_master_add_with_match() returns 0 and the bind
	 * happens later, when the last component calls component_add(). That
	 * is why attach succeeding here does not mean the display is up.
	 */
	error = vc4_newbus_attach(dev, &vc4_platform_driver, "vc4");
	if (error != 0)
		return (error);

	if (bootverbose)
		device_printf(dev, "vc4: master registered; waiting on "
		    "components\n");
	return (0);
}

static int
vc4_master_newbus_detach(device_t dev)
{

	return (vc4_newbus_detach(dev, &vc4_platform_driver));
}

static device_method_t vc4_master_newbus_methods[] = {
	DEVMETHOD(device_probe,		vc4_master_newbus_probe),
	DEVMETHOD(device_attach,	vc4_master_newbus_attach),
	DEVMETHOD(device_detach,	vc4_master_newbus_detach),
	DEVMETHOD_END
};

static driver_t vc4_master_newbus_driver = {
	"vc4",
	vc4_master_newbus_methods,
	sizeof(struct vc4_newbus_softc),
};

/*
 * BUS_PASS_SUPPORTDEV so the master attaches after the buses its components
 * live on have enumerated. It does not need to attach after the components
 * themselves -- an incomplete match list simply waits.
 */
EARLY_DRIVER_MODULE(vc4, simplebus, vc4_master_newbus_driver, 0, 0,
    BUS_PASS_SUPPORTDEV);
EARLY_DRIVER_MODULE(vc4_ofwbus, ofwbus, vc4_master_newbus_driver, 0, 0,
    BUS_PASS_SUPPORTDEV);
