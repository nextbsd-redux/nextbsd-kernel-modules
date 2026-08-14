# drm_extra_helpers -- the DRM GEM/KMS helper layer drm-kmod does not compile.
#
# drm-kmod (6.12-lts) ships ttm/ + drm_gem_ttm_helper, but not the convenience
# helpers the small virtual-GPU drivers sit on. Per the graphics plan, they are
# built ONCE here and exported, rather than bundled into each driver -- two
# drivers each carrying drm_gem_vram_helper would collide on EXPORT_SYMS.
#
#   drm_simple_kms_helper.c   pure drm-core, low risk        (bochs)
#   drm_gem_vram_helper.c     TTM-coupled VRAM GEM, the gate (bochs, vboxvideo)
#
# Sources are vendored UNMODIFIED from Linux v6.12 (matching drm-kmod's
# 6.12-lts). Modelled on drm-kmod's own dummygfx/Makefile.
.include "../kconfig.mk"

SRCDIR=	${.CURDIR}

# The .c files live in the vendored drivers/gpu/drm tree, the FreeBSD glue here.
.PATH:	${SRCDIR} ${SRCDIR:H}/drivers/gpu/drm

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
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/common/include

CFLAGS+= -I${.CURDIR:H}/include
CFLAGS+= -I${.CURDIR:H}/include/drm
CFLAGS+= -I${.CURDIR:H}/include/uapi
CFLAGS+= -I${SRCDIR:H}/drivers/gpu

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=drm_extra_helpers_'
CFLAGS+= -include ${SRCDIR:H}/drivers/gpu/drm/drm_os_config.h

CWARNFLAGS += -Wno-cast-qual
