---
name: module-private-linuxkpi
description: Add LinuxKPI a vendored driver needs without patching the FreeBSD kernel, by shadowing headers from the module's own include path. Use when a driver in graphics/ fails to build for missing LinuxKPI (platform_device, component, of, pm_runtime, interrupt), or when deciding whether an addition belongs in the kernel patch series or in the module.
---

# Module-private LinuxKPI

When a vendored driver needs KPI FreeBSD does not have, the reflex is to add it
to `sys/compat/linuxkpi` in nextbsd-kernel. Resist it. That is shared by every
LinuxKPI consumer, and every regression this project has shipped came from
doing exactly that -- an include collision that broke the whole amdgpu build,
and a `struct device` field insertion that page-faulted `i915kms` on a Wyse 5070
(gershwin-desktop#49). Neither had anything to do with the driver the patch
was for.

## How it works

Every module here is built with its own include paths ahead of the kernel's:

	CFLAGS+= -I${.CURDIR:H}/lkpi -DLKPI_PFX=${KMOD}_    <- yours, FIRST
	CFLAGS+= -I${.CURDIR:H}/linuxkpi/gplv2/include         drm-kmod's
	CFLAGS+= -I${SYSDIR}/compat/linuxkpi/common/include    the kernel's

so `graphics/lkpi/linux/component.h` shadows the kernel's `<linux/component.h>`
for objects compiled into your module, and for nothing else.

**This isolation is real, and it is worth knowing why.** i915, amdgpu and
radeonkms are built in the *drm-kmod tree*, not in `graphics/`, so they never
see this path at all. The modules in `graphics/` that do are vc4-only.

The `-I` must stay first. If it falls below the kernel's path the module
silently builds against the kernel's smaller KPI instead, and you get confusing
errors about things you thought you had defined.

## Symbol collisions

The kernel still exports its own `component_add()`, `of_match_device()` and so
on. If two modules also define them, `kldload` sees duplicates -- these modules
build with `EXPORT_SYMS=YES`, which exports everything.

So the shadowing headers rename every symbol the module provides:

	#define LKPI_SYM2(p, n) p ## n
	#define LKPI_SYM1(p, n) LKPI_SYM2(p, n)
	#define LKPI_SYM(n)     LKPI_SYM1(LKPI_PFX, n)
	#define component_add   LKPI_SYM(component_add)

`LKPI_PFX` comes from the Makefile (`-DLKPI_PFX=${KMOD}_`), so each module gets
its own names. Vendored sources keep calling the Linux spelling, and because the
implementation `.c` files include the same headers, their *definitions* are
renamed by the preprocessor too -- you do not edit them.

Consequence worth knowing: each module gets its own component list and its own
masters. One driver's bind cannot disturb another's. That is a feature.

## What you must NOT shadow

**Only shadow headers whose consumers are all inside your module.**

`dma-mapping.h`, `mm.h` and `iosys-map.h` look like fair game -- vc4 needs
`dma_alloc_wc()` and friends. They are not. Their real consumers are drm-kmod's
own sources (`drm_gem_dma_helper.c`, `drm_fb_dma_helper.c`,
`drm_gem_shmem_helper.c`), and drm-kmod ships its **own** copies of those
headers in `linuxkpi/gplv2/include`. Shadowing them from a module's `lkpi/`
overrides drm-kmod's versions too and loses whatever drm-kmod added -- this
broke `drm-kmod build` on both arches.

Rule of thumb: if the header is one drm core includes, leave it alone. Additions
to those still belong in the kernel series, or in a drm-kmod patch under
`graphics/drm-kmod-patches/`.

**Never shadow `<linux/device.h>`.** `struct device` crosses module boundaries --
`drm.ko` allocates them and hands them to you. A different layout on each side is
an ABI mismatch, not isolation.

## Getting at the device tree without a struct field

Vendored platform drivers use `dev->of_node`. Do not add that field to
`struct device`; that is the exact change that broke i915.

Use `dev_of_node(dev)` -- a real upstream accessor -- backed module-side by a
small `struct device *` -> `struct device_node *` table the newbus shims fill in
at attach (`graphics/lkpi/lkpi_of.c`). A vc4 pipeline is five devices, so a
list is the right shape. Convert vendored `dev->of_node` uses to
`dev_of_node(dev)`; that is idiomatic upstream, not a deviation.

## When it genuinely has to be in the kernel

Allocator plumbing that needs state private to `linux_pci.c` -- the busdma tags,
the `dma_priv` pctrie, the bounce bookkeeping. `linux_dma_alloc_wc()` and
`linux_dma_priv_init()` are the examples. A module cannot reach that state, and
the plausible substitute for `dma_alloc_wc()` (allocate coherent, then
`pmap_change_attr()` to write-combining) rewrites the live allocation path for
every buffer X maps. On arm64 `VM_MEMATTR_WRITE_COMBINING` is write-through
while the coherent allocator is write-back, so it is not a safe alias.

Keep such patches additive -- new functions only, never a struct change.

## Build ordering

`drm-kmod build` downloads the kernel obj from the `continuous` release and
compiles against **those** headers. It cannot see an unmerged kernel PR. So a
change split across both repos has to land module-side first (the module must be
self-sufficient), then kernel-side, then be re-verified once
`publish continuous` has run.
