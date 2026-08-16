# vboxvideo -- KMS driver for the VirtualBox virtual GPU (PCI 80ee:beef).
#
# The cheapest driver in the plan, and cheap *because* bochs went first: it
# needs drm_simple_kms_helper + drm_gem_vram_helper + drm_gem_framebuffer_helper,
# all three of which already build and ship as drm_extra_helpers.ko
# (IOGraphicsExtras.kext). The driver source itself is ~2100 LOC of pure MMIO
# plus the HGSMI host channel -- no command FIFO, no firmware blobs.
#
# Two things bochs did NOT need are pulled in here:
#
#   lib/genalloc.c   Linux' general-purpose allocator. vboxvideo carves the
#                    HGSMI guest heap out of the VRAM tail with a gen_pool,
#                    and linuxkpi ships no gen_pool. Vendored UNMODIFIED apart
#                    from one guarded include; kept in THIS module rather than
#                    drm_extra_helpers because vboxvideo is currently its only
#                    consumer -- move it there if a second driver wants it.
#
#   include/uapi/linux/vbox_err.h
#                    The VirtualBox guest status codes (VINF_SUCCESS, VERR_*).
#                    A uapi header on Linux, which is why the driver spells it
#                    <linux/vbox_err.h>; -I../include/uapi reproduces the same
#                    resolution Linux' exported-header step gives it.
#
# Source vendored from Linux v6.12, matching drm-kmod's 6.12-lts.
#
# Modelled on drm-kmod's i915/Makefile, NOT dummygfx/Makefile: dummygfx is stale
# (it is absent from DEFAULT_KMODS and -include's a drm_os_config.h that no
# longer exists in the tree, which is exactly how the first attempt failed).
.include "../kconfig.mk"

# SRCDIR points into the vendored source tree, as i915's does; the FreeBSD glue
# lives in this module directory. LIBDIR is the second vendored root -- Linux
# keeps genalloc.c in lib/, not under drivers/gpu, and mirroring that layout
# keeps the file diffable against upstream.
SRCDIR=	${.CURDIR:H}/drivers/gpu/drm/vboxvideo
LIBDIR=	${.CURDIR:H}/lib

.PATH:	${SRCDIR} ${LIBDIR} ${.CURDIR}

KMOD=	vboxvideo

EXPORT_SYMS=	YES

# Mirrors upstream drivers/gpu/drm/vboxvideo/Makefile (vboxvideo-y), plus
# genalloc.c and the FreeBSD glue.
SRCS=	\
	hgsmi_base.c \
	modesetting.c \
	vbva_base.c \
	vbox_drv.c \
	vbox_hgsmi.c \
	vbox_irq.c \
	vbox_main.c \
	vbox_mode.c \
	vbox_ttm.c \
	genalloc.c \
	vboxvideo_freebsd.c

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

# ../include and ../include/uapi come AFTER the linuxkpi paths on purpose: the
# vendored headers here (linux/genalloc.h, linux/vbox_err.h) exist precisely
# because linuxkpi has no such file, so they can only ever be found, never
# shadow one. Adding them earlier is how the bochs port shadowed linuxkpi's own
# video/vga.h and had to be backed out.
CFLAGS+= -I${.CURDIR:H}/include
CFLAGS+= -I${.CURDIR:H}/include/drm
CFLAGS+= -I${.CURDIR:H}/include/uapi
# drivers/gpu must be on the path: drm-kmod's own headers include
# <drm/drm_os_freebsd.h>, which lives at drivers/gpu/drm/. Spelled from the
# drm-kmod root rather than ${SRCDIR:H:H} -- SRCDIR sits at a different depth
# per module, and the i915-derived :H:H landed on drivers/ for the helpers,
# which is how iteration 2 of the helper module failed.
CFLAGS+= -I${.CURDIR:H}/drivers/gpu -I${SRCDIR}

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=vboxvideo_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

# The same set every drm-kmod driver applies to vendored Linux sources
# (i915/Makefile:386). Linux code does void-pointer arithmetic freely and uses
# Linux' printk length modifiers, neither of which survives the kernel build's
# -Werror.
CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
