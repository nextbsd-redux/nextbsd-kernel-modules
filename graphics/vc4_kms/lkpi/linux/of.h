/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2023 Serenity Cyber Security, LLC.
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

#ifndef _LINUXKPI_LINUX_OF_H
#define	_LINUXKPI_LINUX_OF_H


/*
 * MODULE-LOCAL LinuxKPI (nextbsd-kernel-extensions#51).
 *
 * This header shadows the kernel's <linux/of.h>. Every drm-kmod module is
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

#define	of_match_device	vc4lkpi_of_match_device
#define	of_parse_phandle	vc4lkpi_of_parse_phandle
#define	of_node_put	vc4lkpi_of_node_put
#define	of_node_get	vc4lkpi_of_node_get
#define	of_find_compatible_node	vc4lkpi_of_find_compatible_node
#define	of_find_matching_node_and_match	vc4lkpi_of_find_matching_node_and_match
#define	of_device_is_available	vc4lkpi_of_device_is_available
#define	of_device_is_compatible	vc4lkpi_of_device_is_compatible
#define	of_device_get_match_data	vc4lkpi_of_device_get_match_data
#define	of_dma_configure	vc4lkpi_of_dma_configure
#include <linux/kobject.h>
#include <linux/types.h>

/*
 * nextbsd-kernel#176: enough of the Linux device-tree model to carry an FDT
 * node through a LinuxKPI driver.
 *
 * struct device_node wraps a FreeBSD phandle_t. It is declared here as an
 * intptr_t rather than including <dev/ofw/openfirm.h>, so this header stays
 * usable on platforms built without FDT -- the implementation in
 * linux_of.c is what knows about OFW, and it compiles to stubs when FDT is
 * not configured.
 */
struct device_node {
	intptr_t	node;		/* phandle_t where FDT is present */
};

struct of_device_id {
	char		compatible[128];
	const void	*data;
};

struct device;

const struct of_device_id *of_match_device(const struct of_device_id *matches,
    const struct device *dev);
struct device_node *of_parse_phandle(const struct device_node *np,
    const char *name, int index);
struct device_node *of_node_get(struct device_node *np);
void of_node_put(struct device_node *np);

/*
 * Node lookup and property queries for the full vc4 KMS pipeline (#51).
 * These walk the live OF tree rather than a driver's own node, which is what a
 * display driver needs to find the HVS and HDMI controllers from a CRTC.
 *
 * Each returns a node the caller owns and must release with of_node_put().
 */
struct device_node *of_find_compatible_node(struct device_node *from,
    const char *type, const char *compat);
struct device_node *of_find_matching_node_and_match(struct device_node *from,
    const struct of_device_id *matches, const struct of_device_id **match);
const void *of_device_get_match_data(const struct device *dev);
bool	of_device_is_available(const struct device_node *np);
bool	of_device_is_compatible(const struct device_node *np, const char *compat);

/*
 * of_dma_configure() sets a device's DMA parameters from its device-tree node.
 * LinuxKPI derives no DMA tag from the tree and linux_dma_priv_init() already
 * establishes the masks a DRM driver needs, so this succeeds without acting.
 *
 * Honest on a SoC where the GPU sees the same physical addresses the CPU does
 * -- BCM2712 has no IOMMU and no DMA offset. A platform with either would be
 * given wrong addresses by this and needs real dma-ranges parsing.
 */
int	of_dma_configure(struct device *dev, struct device_node *np,
	    bool force_dma);

#endif
