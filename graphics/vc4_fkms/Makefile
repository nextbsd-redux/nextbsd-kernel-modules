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

.PATH:	${SRCDIR} ${.CURDIR} ${.CURDIR:H}/lkpi

KMOD=	vc4_fkms

EXPORT_SYMS=	YES

# Implementations behind the private LinuxKPI headers below. Required, not
# optional: those headers #define component_add() and friends onto
# ${KMOD}_-prefixed symbols, so without these the module builds and then fails
# to link against names nothing defines.
SRCS+=	\
	lkpi_component.c \
	lkpi_of.c

SRCS=	\
	vc4_firmware_kms.c \
	vc4_fkms_freebsd.c \
	vc4_fkms_master.c

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
	vnode_if.h		\
	clknode_if.h

# clknode_if.h is GENERATED from dev/clk/clknode_if.m. linux/clk.h is a real
# implementation over clk(9) as of #51, and it includes dev/clk/clk.h which
# includes this -- so every module including linux/clk.h needs it generated,
# even one that (like this driver) never calls a clk_* function. Without it the
# build dies at the #include, which is how it was found: adding the clk shim
# for the full KMS port broke THIS module's build.
CLEANFILES+= ${KMOD}.ko.full ${KMOD}.ko.debug

.include <bsd.kmod.mk>

# Included after <bsd.kmod.mk> because they expand variables it defines.
.include "../linuxkpi_version.mk"
.include "../compiler_flags.mk"

# PRIVATE LinuxKPI, ahead of drm-kmod's and the kernel's.
#
# ../lkpi/linux/*.h shadow the kernel's copies for objects compiled into THIS
# module only. i915, amdgpu and radeonkms are built in the drm-kmod tree and
# never see this path, so nothing here can reach them -- which is the point.
# These additions used to be kernel patches against
# sys/compat/linuxkpi/common/include, where one of them collided with
# LinuxKPI's struct resource and broke the whole amdgpu build.
#
# LKPI_PFX makes the symbols unique per module, since several modules compile
# this code and all export their symbols.
#
# This -I MUST stay first, or the module silently builds against the kernel's
# smaller KPI instead.
CFLAGS+= -I${.CURDIR:H}/lkpi -DLKPI_PFX=${KMOD}_

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

# FreeBSD has an MMU. Without this, drm_gem_dma_helper.h's
# DEFINE_DRM_GEM_DMA_FOPS emits .get_unmapped_area, which is the no-MMU path
# and a field linuxkpi's struct file_operations does not have.
CFLAGS+= -DCONFIG_MMU=1

# The hw.vc4_fkms sysctl node has to be visible in the vendored driver's
# translation unit, which is not ours to edit -- see vc4_fkms_param.h.
CFLAGS+= -include ${.CURDIR}/vc4_fkms_param.h

# vc4_firmware_kms.c:1951 does
#     fkms->revision = (enum vc4_fkms_revision)match->data;
# where of_device_id::data is a const void *. That is how Linux stores
# per-variant data in a match table, and narrowing it to an enum is what every
# driver using the pattern does; clang objects to the width, not the intent.
CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
CWARNFLAGS += -Wno-void-pointer-to-enum-cast
