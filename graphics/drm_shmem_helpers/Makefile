# drm_shmem_helpers -- the shmem GEM layer drm-kmod does not ship at all.
#
#   drm_gem_shmem_helper.c   ~780 LOC, swap-backed GEM objects   (virtio-gpu)
#   drm_fbdev_shmem.c        fbdev emulation over shmem GEM      (virtio-gpu)
#
# This is the single largest omission in drm-kmod for small virtual GPUs.
# drm_gem_shmem_helper.c is not in that tree in any form -- not built, not
# present -- and six candidate drivers sit on it: virtio-gpu, Hyper-V, ast,
# mgag200, cirrus and simpledrm. Paying for it once here is what makes the
# others cheap, exactly as drm_extra_helpers did for bochs and vboxvideo.
#
# drm_fbdev_shmem.c is deliberately NOT built, though drm-kmod ships the file
# and compiles it into nothing. Measured: it needs fbdev DEFERRED I/O, which
# drm-kmod's fbdev emulation does not have -- fb_deferred_io_{init,cleanup} are
# undeclared and neither struct drm_fb_helper nor struct linux_fb_info carries
# an fbdefio member. That was 8 of the 15 errors in the first CI run.
#
# The cost of leaving it out is fbcon on the virtio GPU, not KMS: the console
# stays on efifb while /dev/dri/card0 serves X11 and Mesa, which is the target.
# virtio-gpu's one call to drm_fbdev_shmem_setup() is #ifdef'd out to match.
# Wiring deferred I/O into drm-kmod's fb_helper is its own piece of work.
#
# Why this is a separate module from drm_extra_helpers: that one is required by
# the boot test, because BochsGraphics resolves through IOGraphicsExtras.
# Adding unproven code to it would let a compile error here take down a driver
# that already works.
#
# Why the shmem helper matters beyond virtio-gpu: its buffers are ordinary
# system memory rather than a device BAR, so nothing on this path needs
# vm_phys_fictitious_reg_range(). That is the defect that bit every VRAM-backed
# driver in this tree (bochs, vboxvideo) and hung X for a week.
#
# LICENCE: drm_gem_shmem_helper.c is SPDX GPL-2.0-only, where the drm_extra
# helpers are GPL-2.0-or-later. lib/genalloc.c, already shipping in
# VBoxGraphics.kext, is the tree's other GPL-2.0-only file. Per-file SPDX
# governs; graphics/README.md records the position.
.include "../kconfig.mk"

SRCDIR=	${.CURDIR:H}/drivers/gpu/drm

.PATH:	${SRCDIR} ${.CURDIR}

KMOD=	drm_shmem_helpers

EXPORT_SYMS=	YES

SRCS=	\
	drm_gem_shmem_helper.c \
	drm_shmem_helpers_freebsd.c

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
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=drm_shmem_helpers_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual
