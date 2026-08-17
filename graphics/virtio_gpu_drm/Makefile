# virtio_gpu_drm -- the Linux virtio-gpu DRM/KMS driver, vendored from v6.12.
#
# NOT named virtio_gpu: FreeBSD base owns that name for its own non-DRM
# virtio_gpu(4), and two modules answering to one name is a coin flip over
# which one binds. The kext bundle is VirtIOGraphics.kext; the DRM driver name
# the userspace ABI keys on stays "virtio_gpu" (DRIVER_NAME in virtgpu_drv.h),
# because that is the contract Mesa's virgl driver already ships against on
# both amd64 and arm64.
#
# This is the first driver in the tree that is not a PCI DRM driver: it binds
# on FreeBSD's virtio bus, so the LinuxKPI-by-name PCI registration path does
# not apply and the newbus glue is hand-written.
#
# Dependencies beyond the usual drm-kmod set:
#
#   linux_virtio.ko        the LinuxKPI virtio shim (graphics/linuxkpi_virtio)
#   drm_shmem_helpers.ko   drm_gem_shmem_helper + drm_fbdev_shmem, neither of
#                          which drm-kmod compiles
#   drm_extra_helpers.ko   drm_simple_encoder_init + drm_gem_fb_create_handle
#
# virtgpu_vram.c is deliberately absent from SRCS. It implements host-visible
# blob resources, which need a get_shm_region bus method FreeBSD's virtio bus
# does not have, so the feature can never be negotiated here; it is also the
# only GPL-2.0-only file in the driver, the rest being MIT. See the stub block
# in virtgpu_drv.h.
#
# virtgpu_trace_points.c is absent for the same reason drm-kmod drops every
# tracepoint TU: Linux' TRACE_EVENT() tree expands against ftrace. The KTR
# replacement lives in virtgpu_trace.h.
.include "../kconfig.mk"

SRCDIR=	${.CURDIR:H}/drivers/gpu/drm/virtio

.PATH:	${SRCDIR} ${.CURDIR}

KMOD=	virtio_gpu_drm

EXPORT_SYMS=	YES

SRCS=	\
	virtgpu_drv.c \
	virtgpu_kms.c \
	virtgpu_gem.c \
	virtgpu_display.c \
	virtgpu_vq.c \
	virtgpu_fence.c \
	virtgpu_object.c \
	virtgpu_debugfs.c \
	virtgpu_plane.c \
	virtgpu_ioctl.c \
	virtgpu_prime.c \
	virtgpu_submit.c \
	virtio_gpu_drm_freebsd.c

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
	virtio_bus_if.h		\
	virtio_if.h		\
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
# <drm/drm_os_freebsd.h>, which lives at drivers/gpu/drm/.
CFLAGS+= -I${.CURDIR:H}/drivers/gpu -I${SRCDIR}

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=virtio_gpu_drm_' -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}

# virtio-gpu's KMS half is conditional on this in the vendored sources; without
# it virtio_gpu_init() sets num_scanouts to 0 and the driver comes up
# render-only, which is not what any of this is for.
CFLAGS+= -DCONFIG_DRM_VIRTIO_GPU_KMS

# The same set every drm-kmod driver applies to vendored Linux sources.
CWARNFLAGS += -Wno-pointer-arith -Wno-format -Wno-cast-qual

# virtio_gpu_driver (the struct virtio_driver) is referenced only by
# module_virtio_driver(), which is #ifdef'd out on FreeBSD -- the binding is
# declared by DRIVER_MODULE() in the glue instead. Keeping the struct rather
# than deleting it is deliberate vendoring discipline; drm-kmod silences the
# same class of warning per-file (drm/Makefile does it for drm_syncobj.c).
CWARNFLAGS.virtgpu_drv.c += -Wno-unused-variable
