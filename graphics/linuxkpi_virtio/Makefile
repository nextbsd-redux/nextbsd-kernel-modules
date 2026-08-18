# linux_virtio -- the LinuxKPI virtio shim.
#
# FreeBSD base ships no linuxkpi virtio: sys/compat/linuxkpi has no
# <linux/virtio.h>, and FreeBSD's native virtio bus presents a completely
# different API. That gap, not the driver, is what has kept every Linux virtio
# driver off drm-kmod.
#
# Built as its own module rather than folded into the driver for the same
# reason drm_extra_helpers is: the next virtio driver ported here (virtio-input
# is the obvious one) needs the identical shim, and two modules each carrying a
# copy would collide on EXPORT_SYMS.
#
# Unlike everything else under graphics/, this is NOT vendored Linux code. It is
# BSD-2-Clause, written against FreeBSD's public virtqueue KPI, which is what
# makes it the one piece of this work that could be proposed to FreeBSD base
# later without a licensing conversation.
.include "../kconfig.mk"

.PATH:	${.CURDIR}

KMOD=	linux_virtio

# Set because the driver module links against every lkpi_* entry point here.
# EXPORT_SYMS restricts rather than enables -- the default of NO strips every
# global symbol, producing a module that loads and satisfies nothing.
EXPORT_SYMS=	YES

SRCS=	linux_virtio.c

SRCS	+=			\
	opt_acpi.h		\
	opt_drm.h		\
	opt_platform.h		\
	bus_if.h		\
	device_if.h		\
	pci_if.h		\
	virtio_bus_if.h		\
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

# Our vendored headers go AFTER the linuxkpi paths, never before: a header of
# ours that shadowed a real linuxkpi one would be found first and silently win.
# That is how the bochs port lost a day to a supplementary video/vga.h.
CFLAGS+= -I${.CURDIR:H}/include
CFLAGS+= -I${.CURDIR:H}/include/uapi

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=linux_virtio_'
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
