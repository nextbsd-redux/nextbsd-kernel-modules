# bochs -- KMS driver for the Bochs/stdvga display (PCI 1234:1111).
#
# P0 of the graphics plan: it binds qemu's DEFAULT -vga std device, so the kext
# device-matches in CI with no qemu reconfiguration, and it matches as a raw
# IOPCIPrimaryMatch node -- reusing the existing match/kextd/load/bind machinery
# unchanged. ~740 LOC, pure MMIO, no command FIFO and no hypervisor backdoor.
#
# Depends on drm_extra_helpers.ko for drm_simple_kms_helper + drm_gem_vram_helper,
# which drm-kmod does not compile. Source vendored UNMODIFIED from Linux v6.12;
# its tail is drm_module_pci_driver(), which drm-kmod's include/drm/drm_module.h
# already provides (mapping onto LinuxKPI module_driver/pci_register_driver), so
# the FreeBSD glue is only the kld dependency edges.
#
# Modelled on drm-kmod's i915/Makefile, NOT dummygfx/Makefile: dummygfx is stale
# (it is absent from DEFAULT_KMODS and -include's a drm_os_config.h that no
# longer exists in the tree, which is exactly how the first attempt failed).
.include "../kconfig.mk"

# SRCDIR points into the vendored source tree, as i915's does; the FreeBSD glue
# lives in this module directory.
SRCDIR=	${.CURDIR:H}/drivers/gpu/drm/tiny

.PATH:	${SRCDIR} ${.CURDIR}

KMOD=	bochs

EXPORT_SYMS=	YES

SRCS=	\
	bochs.c \
	bochs_freebsd.c

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
CFLAGS+= -I${SRCDIR:H:H} -I${SRCDIR}

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=bochs_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

CWARNFLAGS += -Wno-cast-qual
