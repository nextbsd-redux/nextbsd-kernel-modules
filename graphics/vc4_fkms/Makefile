# vc4_fkms -- COMPILE PROBE for vc4_firmware_kms (nextbsd-kernel#176).
#
# This is not a port and does not produce a loadable driver. It exists to make
# the compiler enumerate what vc4_firmware_kms.c is missing, the same way the
# drm_dma_helpers build did: guessing at a 53-symbol DRM surface from the
# outside was going to be wrong, so let clang produce the list instead.
#
# Measured before writing any of this, on the 2079-line source:
#
#   clk_*                       0 calls   -- include only, hence an empty clk.h
#   component_add/del           2 calls   -- no master, no match arrays
#   rpi_firmware_*             11 calls   -- the real work, over a mailbox
#                                            NextBSD already has as
#                                            bcm2835_firmware_property()
#   drm_*/drmm_*               53 distinct functions
#
# The three headers under ../include that this needs -- linux/clk.h,
# linux/component.h and soc/bcm2835/raspberrypi-firmware.h -- are declarations
# only. Undefined symbols at link time are therefore expected and are part of
# what this probe measures; the compile errors are the interesting half.
#
# Sources vendored UNMODIFIED from raspberrypi/linux rpi-6.12.y, matching
# drm-kmod's 6.12-lts.

.include "../kconfig.mk"

SRCDIR=	${.CURDIR:H}/drivers/gpu/drm/vc4

.PATH:	${SRCDIR} ${.CURDIR}

KMOD=	vc4_fkms

EXPORT_SYMS=	YES

SRCS=	\
	vc4_firmware_kms.c \
	vc4_fkms_freebsd.c

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
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=vc4_fkms_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}
# Without this the vendored raspberrypi-firmware.h supplies static inlines
# returning -ENOSYS, and all 11 rpi_firmware_* call sites bind to stubs --
# which is exactly what the first compile probe did, silently.
CFLAGS+= -DCONFIG_RASPBERRYPI_FIRMWARE=1

CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
