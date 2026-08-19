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
# drm_fbdev_shmem.c IS built, from a vendored copy under ../drivers/gpu/drm/
# that guards four blocks with #ifdef __linux__ -- the same thing drm-kmod
# already does in the sibling drm_fbdev_ttm.c, which it does build.
#
# The earlier note here said this needed fbdev DEFERRED I/O first. That was
# wrong. Everything in Linux's fb_defio.c serves a userspace mmap() of
# /dev/fbN, and drm-kmod creates no such cdev; the console damage path runs
# through fb_ops -> drm_fb_helper_damage_area() -> fb_dirty ->
# drm_atomic_helper_dirtyfb, which is implemented and exported already.
#
# It also said the cost was "fbcon on the virtio GPU, not KMS: the console
# stays on efifb". That premise does not hold on arm64: qemu's OVMF gives a
# Blt-only GOP for virtio-gpu (no linear framebuffer, so no efifb), and Apple's
# Virtualization.framework publishes no GOP at all. With no efifb anywhere,
# this fbdev is the only console such a guest can have once the kext owns the
# device.
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
	drm_fbdev_shmem.c \
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
