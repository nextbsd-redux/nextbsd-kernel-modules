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
#   drm_gem_atomic_helper.c   shadow-buffered planes          (vboxvideo)
#
# The shadow-plane layer was added for vboxvideo, whose cursor plane is built
# on DRM_GEM_SHADOW_PLANE_FUNCS. drm-kmod's include/drm/drm_gem_atomic_helper.h
# is a 15-line stub declaring only drm_gem_plane_helper_prepare_fb(), so the
# whole shadow API -- struct drm_shadow_plane_state, begin/end_shadow_fb_access,
# the FUNCS macros -- is missing. The vendored .c excludes that ONE function
# (drm.ko already exports it) and keeps everything after it. Every small driver
# after this one -- ast, mgag200, virtio-gpu -- uses shadow planes too, which is
# exactly why it belongs here rather than inside vboxvideo.
#
#   drm_format_helper.c       format conversion + conv_state  (shadow planes)
#
# The shmem GEM helpers are NOT here, in a companion module instead
# (graphics/drm_shmem_helpers). They belong to the same category and the
# instinct is to add them to this list, but this module is load-bearing: the
# boot test requires IOGraphicsExtras.kext, because BochsGraphics resolves
# through it. Folding an unproven ~780-line helper into it means a compile
# error in the new code takes down the driver that already works. Separate
# modules, separate blast radius.
#
# drm_format_helper.c is NOT vendored: drm-kmod already carries a FreeBSD-adapted
# copy at drivers/gpu/drm/, which .PATH picks up. It is listed here because
# drm-kmod compiles it into NOTHING -- the file sits in the tree with its
# EXPORT_SYMBOLs intact, but no Makefile in that repo (drm, i915, amd, radeon)
# names it, so drm_format_conv_state_{init,copy,release} exist in source and in
# no .ko. struct drm_shadow_plane_state embeds a drm_format_conv_state and the
# shadow duplicate/reset/destroy paths call all three, so without this line
# drm_extra_helpers.ko builds clean and then fails kldload with ENOEXEC on three
# undefined symbols -- which is exactly how the first attempt at this failed.
# Verified against the built kexts that nothing else defines them, so there is
# no duplicate-symbol risk in claiming them here.
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
	drm_gem_framebuffer_helper.c \
	drm_gem_atomic_helper.c \
	drm_format_helper.c \
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
# drivers/gpu must be on the path: drm-kmod's own headers include
# <drm/drm_os_freebsd.h>, which lives at drivers/gpu/drm/. Spelled from the
# drm-kmod root rather than ${SRCDIR:H:H} -- SRCDIR sits at a different depth
# per module (drivers/gpu/drm vs drivers/gpu/drm/tiny), and the i915-derived
# :H:H landed on drivers/ for the helpers, which is how iteration 2 failed.
CFLAGS+= -I${.CURDIR:H}/drivers/gpu -I${SRCDIR}

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=drm_extra_helpers_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

# The same set every drm-kmod driver applies to vendored Linux sources
# (i915/Makefile:386). Linux code does void-pointer arithmetic freely and uses
# Linux' printk length modifiers, neither of which survives the kernel build's
# -Werror: iteration 4 failed on -Wgnu-pointer-arith alone, and for the helpers
# the offending code was inside FreeBSD's OWN linux/iosys-map.h rather than
# anything vendored here.
CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
