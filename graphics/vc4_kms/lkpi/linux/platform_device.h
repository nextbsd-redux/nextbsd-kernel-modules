/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020-2022 Bjoern A. Zeeb
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_LINUXKPI_LINUX_PLATFORM_DEVICE_H
#define	_LINUXKPI_LINUX_PLATFORM_DEVICE_H


/*
 * MODULE-LOCAL LinuxKPI (nextbsd-kernel-extensions#51).
 *
 * This header shadows the kernel's <linux/platform_device.h>. Every drm-kmod module is
 * built with its own include paths ahead of the kernel's:
 *
 *	-I linuxkpi/gplv2/include			drm-kmod's own KPI
 *	-I linuxkpi/bsd/include				drm-kmod's own KPI
 *	-I ${SYSDIR}/compat/linuxkpi/common/include	the kernel's
 *
 * and this module puts -I${.CURDIR}/lkpi ahead of all of them, so what
 * follows is seen ONLY by objects compiled into this module. drm.ko, i915,
 * amdgpu and the kernel itself are untouched.
 *
 * That isolation is the entire point. Eleven of the kernel's LinuxKPI patches
 * touched shared headers and both regressions we have had came from those
 * eleven -- an include collision that broke the whole amdgpu build, and a
 * struct device field that broke KBI for every already-built module. This
 * code carries the same risk and none of the blast radius.
 *
 * The kernel still exports its own component_add(), of_match_device() and so
 * on, from patch 0040, which firmware KMS runs on. Ours would collide at load
 * time, so every function this module provides is renamed to vc4lkpi_* below
 * and the Linux spelling is #defined onto it. Vendored sources keep calling
 * the Linux names; the preprocessor renames the definitions in our .c files
 * for free, because they include this header too.
 */

#define	lkpi_platform_get_irq	vc4lkpi_platform_get_irq
#define	lkpi_platform_get_irq_byname	vc4lkpi_platform_get_irq_byname
#define	lkpi_platform_ioremap_resource	vc4lkpi_platform_ioremap_resource
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>

struct platform_device {
	const char			*name;
	int				id;
	bool				id_auto;
	struct device			dev;
};

struct platform_driver {
	/*
	 * nextbsd-kernel#176: probe was absent. A driver assigning it is a
	 * hard error -- but only if the compiler reaches the initialiser, and
	 * it does not once an earlier error in the same file has poisoned the
	 * function's type, which is why the fkms probe never reported this.
	 * It is the entry point that starts a driver, so it has to be here.
	 */
	int				(*probe)(struct platform_device *);
	void				(*remove)(struct platform_device *);
	/*
	 * nextbsd-kernel-extensions#51: upstream renamed .remove to .remove_new
	 * during the transition to a void return, and 6.12-era drivers assign
	 * the new name -- vc4_hvs, vc4_crtc and vc4_hdmi all do. Both are
	 * carried because the tree contains drivers written against each.
	 *
	 * Same signature as .remove here: LinuxKPI's .remove already returns
	 * void, so this is a spelling difference rather than a behavioural one.
	 */
	void				(*remove_new)(struct platform_device *);
	struct device_driver		driver;
};

/*
 * nextbsd-kernel#176: to_platform_device() returned NULL, which is a
 * placeholder rather than a definition -- every Linux platform driver starts
 * its probe with it, so any driver reaching this either crashed or silently
 * did nothing. struct platform_device already embeds struct device, so the
 * cast is the ordinary container_of.
 */
/*
 * dev_is_platform() stays false. There is no flag on struct device saying it
 * came from the platform bus, and answering true unconditionally would make
 * drm_aperture's "the given device is not a platform device" guard pass for a
 * PCI device -- which is worse than the conservative answer. Nothing in this
 * tree needs it to be true.
 */
#define	dev_is_platform(dev)	(false)

/*
 * The parameter is _dev, not dev: `dev` is also the member name, so a macro
 * parameter called dev is substituted into the member argument as well.
 * to_platform_device(drm->dev) then expands to
 * container_of(drm->dev, struct platform_device, drm->dev), which fails in
 * four different ways at once. That is exactly how this broke drm_aperture.c
 * after the first version landed.
 */
#define	to_platform_device(_dev)	\
	container_of(_dev, struct platform_device, dev)

static __inline void *
platform_get_drvdata(const struct platform_device *pdev)
{

	return (dev_get_drvdata(&pdev->dev));
}

static __inline void
platform_set_drvdata(struct platform_device *pdev, void *data)
{

	dev_set_drvdata(&pdev->dev, data);
}

/*
 * Linux numbers a platform device's interrupts from 0. FreeBSD hands them out
 * as bus resources, so the glue that creates the platform_device is what
 * allocates them and records the first in dev->irq; this returns that.
 *
 * Only index 0 is answered, because that is all struct device carries and all
 * any consumer here asks for. A higher index gets -ENXIO rather than a wrong
 * answer.
 */
/*
 * Interrupts of a platform device.
 *
 * Both are out of line, in linux_of.c, because they read the device tree --
 * platform_get_irq_byname() resolves a name against the node's
 * interrupt-names property, which is how a DT driver asks for one of several
 * interrupts without hardcoding an index.
 *
 * They return a tagged rid (see LKPI_IRQ_OF in <linux/device.h>), not an
 * interrupt number: request_irq() needs to know WHICH resource to allocate,
 * and a device off the PCI bus has no other way to say so. A device with one
 * unnamed interrupt still works through dev->irq as before.
 */
int	lkpi_platform_get_irq(struct platform_device *pdev, unsigned int num);
int	lkpi_platform_get_irq_byname(struct platform_device *pdev,
	    const char *name);

static __inline int
platform_get_irq(struct platform_device *pdev, unsigned int num)
{

	return (lkpi_platform_get_irq(pdev, num));
}

static __inline int
platform_get_irq_byname(struct platform_device *pdev, const char *name)
{

	return (lkpi_platform_get_irq_byname(pdev, name));
}

/*
 * Map a platform device's Nth memory resource (#51).
 *
 * "devm" does NOT auto-release here: LinuxKPI devres does not track bus
 * resources, so a driver that maps and never unmaps leaks the mapping for the
 * module's lifetime. Fine for a display driver mapping registers at attach,
 * which is every vc4 caller -- stated so the next caller knows.
 */
/*
 * Map a platform device's memory resource.
 *
 * The body is OUT OF LINE, in linux_of.c, and that is not a style choice.
 *
 * It needs FreeBSD's struct resource (sys/rman.h). LinuxKPI declares its OWN
 * struct resource in <linux/ioport.h> -- a different type with different
 * members. Pulling sys/rman.h in from this header put the FreeBSD definition
 * ahead of the LinuxKPI one for every consumer, and any driver reaching
 * ioport.h afterwards failed to build with "redefinition of 'resource'"; that
 * is exactly the include path <linux/mfd/core.h> takes, so it broke all of
 * amdgpu. Even where it compiled, "struct resource" inside an inline here
 * would mean whichever definition happened to be in scope at the call site.
 *
 * Keeping the two definitions apart means keeping sys/rman.h out of this
 * header. The return value is the mapped virtual address readl()/writel()
 * expect, or an ERR_PTR().
 */
void	*lkpi_platform_ioremap_resource(struct platform_device *pdev,
	    unsigned int index);

static __inline void __iomem *
devm_platform_ioremap_resource(struct platform_device *pdev, unsigned int index)
{

	return (lkpi_platform_ioremap_resource(pdev, index));
}

/*
 * platform_find_device_by_driver() is deliberately NOT implemented (#51).
 *
 * vc4_drv.c uses it to enumerate every device bound to a component driver.
 * There is nothing to enumerate: platform_driver_register() below is a stub
 * returning -ENXIO, so no platform driver is ever bound and no
 * device_t -> struct device registry exists. Returning NULL would compile and
 * then silently build an empty component match list, failing later and further
 * away. Left as a compile error until the platform bus underneath is real.
 */

static __inline int platform_driver_register(struct platform_driver *pdrv);
static __inline void platform_driver_unregister(struct platform_driver *pdrv);

/*
 * Register/unregister an array of platform drivers as one unit (#51). A
 * failure part-way unwinds exactly what succeeded, not the whole array.
 */
static __inline void
platform_unregister_drivers(struct platform_driver *const *drivers, unsigned int count)
{

	while (count--)
		platform_driver_unregister(drivers[count]);
}

static __inline int
platform_register_drivers(struct platform_driver *const *drivers, unsigned int count)
{
	unsigned int i;
	int error;

	for (i = 0; i < count; i++) {
		error = platform_driver_register(drivers[i]);
		if (error != 0) {
			platform_unregister_drivers(drivers, i);
			return (error);
		}
	}
	return (0);
}

static __inline int
platform_driver_register(struct platform_driver *pdrv)
{

	pr_debug("%s: TODO\n", __func__);
	return (-ENXIO);
}

static __inline void *
dev_get_platdata(struct device *dev)
{

	pr_debug("%s: TODO\n", __func__);
	return (NULL);
}

static __inline int
platform_driver_probe(struct platform_driver *pdrv,
    int(*pd_probe_f)(struct platform_device *))
{

	pr_debug("%s: TODO\n", __func__);
	return (-ENODEV);
}

static __inline void
platform_driver_unregister(struct platform_driver *pdrv)
{

	pr_debug("%s: TODO\n", __func__);
	return;
}

static __inline int
platform_device_register(struct platform_device *pdev)
{
	pr_debug("%s: TODO\n", __func__);
	return (0);
}

static __inline void
platform_device_unregister(struct platform_device *pdev)
{

	pr_debug("%s: TODO\n", __func__);
	return;
}

#endif	/* _LINUXKPI_LINUX_PLATFORM_DEVICE_H */
