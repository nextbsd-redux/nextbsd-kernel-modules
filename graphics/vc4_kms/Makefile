# vc4_kms -- COMPILE PROBE for the full vc4 KMS display pipeline
# (nextbsd-kernel-extensions#51).
#
# Not a port and not a loadable driver. Firmware KMS (#44) brought the device
# up but cannot program a plane: the 2712 firmware does not service SET_PLANE
# (#48, measured). The real display pipeline programs the HVS and HDMI PHY
# directly instead of asking the firmware, so it is a different driver.
#
# This exists to make clang enumerate what that driver is missing, exactly as
# the #176 probe did -- that probe turned "port a DRM driver" into a measured
# list of 15 device-model errors and zero missing DRM symbols, which is a far
# better basis for scoping than reading 450KB of source.
#
# Display-only subset. Deliberately NOT vendored:
#   vc4_v3d.c, vc4_validate*.c, vc4_render_cl.c, vc4_perfmon.c   -- 3D
#   vc4_dsi.c, vc4_dpi.c, vc4_vec.c, vc4_txp.c                   -- non-HDMI outputs
#
# 2712 is supported upstream: vc4_of_match carries "brcm,bcm2712-vc6" and the
# DMA range table "brcm,bcm2712-hvs", so this is the right driver for a Pi 5.
#
# Sources vendored UNMODIFIED from raspberrypi/linux rpi-6.12.y, matching
# drm-kmod's 6.12-lts.

.include "../kconfig.mk"

SRCDIR=	${.CURDIR:H}/drivers/gpu/drm/vc4

.PATH:	${SRCDIR} ${.CURDIR}

KMOD=	vc4_kms

EXPORT_SYMS=	YES

SRCS=	\
	vc4_drv.c \
	vc4_kms.c \
	vc4_crtc.c \
	vc4_hvs.c \
	vc4_plane.c \
	vc4_hdmi.c \
	vc4_hdmi_phy.c \
	vc4_bo.c \
	vc4_gem.c \
	vc4_fence.c

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
	ofw_bus_if.h		\
	vnode_if.h

CLEANFILES+= ${KMOD}.ko.full ${KMOD}.ko.debug

.include <bsd.kmod.mk>

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
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=vc4_kms_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}
CFLAGS+= -DCONFIG_RASPBERRYPI_FIRMWARE=1
CFLAGS+= -DCONFIG_MMU=1

# Same narrowing pattern as the fkms probe: of_device_id::data is a
# const void * and drivers cast it to a per-variant enum.
# A probe measures the WHOLE gap, so do not let clang stop at its default
# 20-error cap -- the first run capped three files at 19 and made the
# surface look smaller than it is.
CFLAGS+= -ferror-limit=0

CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
CWARNFLAGS += -Wno-void-pointer-to-enum-cast
