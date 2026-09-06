# drm_dma_helpers -- the contiguous-DMA GEM layer drm-kmod does not ship.
#
#   drm_gem_dma_helper.c   604 LOC, physically contiguous GEM objects
#   drm_fb_dma_helper.c    195 LOC, framebuffer accessors over those objects
#
# Needed by every DRM driver that scans out of contiguous memory rather than
# through an IOMMU or TTM -- which on this hardware means the Raspberry Pi
# display path (nextbsd-kernel#176). vc4 and its firmware-KMS sibling both sit
# on drm_gem_dma_object and drm_fb_dma_get_gem_addr.
#
# Verified absent from drm-kmod 6.12-lts rather than assumed: that tree's
# drivers/gpu/drm/ holds 80 .c files and none of them matches dma or cma, and
# include/drm/ has no drm_gem_dma_helper.h either. Linux renamed this layer
# from CMA to DMA in 6.2, so neither spelling is present.
#
# Everything these two need IS already there: drm_panic.h, drm_damage_helper.h,
# drm_gem_framebuffer_helper.h and drm_vma_manager.h all ship in drm-kmod's
# include/drm, and linux/dma-mapping.h is in base LinuxKPI. linux/dma-buf.h is
# not in base, but drm-kmod carries its own under linuxkpi/gplv2/include, which
# the -I lines below already pick up.
#
# A separate module from drm_extra_helpers and drm_shmem_helpers, for the reason
# drm_shmem_helpers records: drm_extra_helpers is load-bearing for the boot test
# via IOGraphicsExtras, so unproven code does not go in it. Separate modules,
# separate blast radius.
#
# Sources are vendored UNMODIFIED from Linux v6.12, matching drm-kmod's 6.12-lts.

.include "../kconfig.mk"

SRCDIR=	${.CURDIR:H}/drivers/gpu/drm

.PATH:	${SRCDIR} ${.CURDIR}

KMOD=	drm_dma_helpers

EXPORT_SYMS=	YES

SRCS=	\
	drm_gem_dma_helper.c \
	drm_fb_dma_helper.c \
	drm_dma_helpers_freebsd.c

SRCS	+=			\
	opt_acpi.h		\
	opt_drm.h		\
	opt_platform.h		\
	opt_splash.h		\
	opt_syscons.h		\
	opt_teken.h		\
	acpi_if.h		\
	bus_if.h		\
	fb_if.h			\
	device_if.h		\
	pci_if.h		\
	vnode_if.h

CLEANFILES+= ${KMOD}.ko.full ${KMOD}.ko.debug

.include <bsd.kmod.mk>

# Included after <bsd.kmod.mk> because they expand variables it defines.
.include "../linuxkpi_version.mk"
.include "../compiler_flags.mk"

CFLAGS+= -I${.CURDIR:H}/linuxkpi/gplv2/include
CFLAGS+= -I${.CURDIR:H}/linuxkpi/bsd/include
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/common/include
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/dummy/include

CFLAGS+= -I${.CURDIR:H}/include
CFLAGS+= -I${.CURDIR:H}/include/drm
CFLAGS+= -I${.CURDIR:H}/include/uapi
CFLAGS+= -I${.CURDIR:H}/drivers/gpu -I${SRCDIR}

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=drm_dma_helpers_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
