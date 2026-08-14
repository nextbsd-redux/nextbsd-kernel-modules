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
# already provides.
.include "../kconfig.mk"

SRCDIR=	${.CURDIR}

# The .c files live in the vendored drivers/gpu/drm tree, the FreeBSD glue here.
.PATH:	${SRCDIR} ${SRCDIR:H}/drivers/gpu/drm/tiny

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
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/common/include

CFLAGS+= -I${.CURDIR:H}/include
CFLAGS+= -I${.CURDIR:H}/include/drm
CFLAGS+= -I${.CURDIR:H}/include/uapi
CFLAGS+= -I${SRCDIR:H}/drivers/gpu

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=bochs_'
CFLAGS+= -include ${SRCDIR:H}/drivers/gpu/drm/drm_os_config.h

CWARNFLAGS += -Wno-cast-qual
