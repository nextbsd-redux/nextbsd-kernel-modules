# drm_extra_helpers -- the DRM GEM/KMS helper layer drm-kmod does not compile.
#
# drm-kmod (6.12-lts) ships ttm/ + drm_gem_ttm_helper, but not the convenience
# helpers the small virtual-GPU drivers sit on. Per the graphics plan, they are
# built ONCE here and exported rather than bundled into each driver: bochs and
# vboxvideo both need the VRAM helper, and two modules each carrying it would
# collide on EXPORT_SYMS.
#
#   drm_simple_kms_helper.c   pure drm-core, low risk         (bochs)
#   drm_gem_vram_helper.c     TTM-coupled VRAM GEM, the gate  (bochs, vboxvideo)
#
# Sources are vendored UNMODIFIED from Linux v6.12, matching drm-kmod's 6.12-lts.
#
# Modelled on drm-kmod's i915/Makefile, NOT dummygfx/Makefile: dummygfx is stale
# (it is absent from DEFAULT_KMODS and -include's a drm_os_config.h that no
# longer exists in the tree, which is exactly how the first attempt failed).
.include "../kconfig.mk"

# SRCDIR points into the vendored source tree, as i915's does; the FreeBSD glue
# lives in this module directory.
SRCDIR=	${.CURDIR:H}/drivers/gpu/drm

.PATH:	${SRCDIR} ${.CURDIR}

KMOD=	drm_extra_helpers

EXPORT_SYMS=	YES

SRCS=	\
	drm_simple_kms_helper.c \
	drm_gem_vram_helper.c \
	drm_extra_helpers_freebsd.c

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
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=drm_extra_helpers_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

CWARNFLAGS += -Wno-cast-qual
