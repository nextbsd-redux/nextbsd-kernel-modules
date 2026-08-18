# graphics/ — vendored DRM pieces drm-kmod does not ship

Staging area for the virtual-GPU KMS work described in
[the graphics plan](https://pkgdemon.github.io/nextbsd-graphics-plan.html).

## Why this exists

drm-kmod (`6.12-lts`) builds the DRM core plus i915/amdgpu/radeon, but its
`drivers/gpu/drm/` holds **no** `tiny/` (bochs), no `virtio`, no `vmwgfx` — and,
more importantly, none of the GEM/KMS convenience helpers those small drivers
sit on. Verified against `6.12-lts` @ `01682db`:

| piece | in drm-kmod 6.12-lts? |
|---|---|
| `ttm/`, `drm_gem_ttm_helper`, atomic/format/fbdev helpers | present |
| `drm_simple_kms_helper.c` (+ header) | **missing** |
| `drm_gem_vram_helper.c` (+ header) | **missing** |
| `drm_gem_shmem_helper.c` | **missing entirely** — not in the tree at all; vendored here for virtio-gpu |
| `drm_fbdev_shmem.c` | present, **compiled by nothing** (`drm/Makefile` builds `drm_fbdev_ttm.c` and `drm_fbdev_client.c`) |
| `linux/virtio*.h`, LinuxKPI virtio | **missing from FreeBSD base**, not just drm-kmod — `sys/compat/linuxkpi` has no virtio at all |
| `drm_gem_framebuffer_helper.c` | **a 76-line stub** — implements 3 of the functions its own header declares; `drm_gem_fb_create` is absent |

So the plan's central finding holds at 6.12: *the cost of a small virtual-GPU
driver is the helper layer, not the driver*.

## Layout

    drivers/gpu/drm/   Linux v6.12 sources; the helpers are verbatim, drivers
                       carry FreeBSD deltas in #ifdef __FreeBSD__ blocks --
                       the same convention drm-kmod uses for its own vendored
                       Linux sources
    include/drm/       their headers, verbatim
    include/linux/     LinuxKPI additions (virtio*.h) -- ours, not vendored
    include/uapi/      Linux uapi headers, verbatim
    drm_extra_helpers/ FreeBSD module: simple-KMS, VRAM GEM, shadow planes
    drm_shmem_helpers/ FreeBSD module: shmem GEM + fbdev-over-shmem
    linuxkpi_virtio/   FreeBSD module: the LinuxKPI virtio shim
    bochs/             \
    vboxvideo/          > driver modules: Makefile + kld glue
    virtio_gpu_drm/    /

Each helper `.ko` compiles its helpers **once** and exports them. Bundling them
per-driver would make two drivers collide on `EXPORT_SYMS`, which is why the
plan calls for shared modules.

The helpers are split across two modules rather than one because
`drm_extra_helpers` is load-bearing: the boot test requires
`IOGraphicsExtras.kext`, since `BochsGraphics` resolves through it. Adding
unproven code to it would let a compile error in a new helper take down a
driver that already works.

## Licensing

Vendored files keep their upstream SPDX line; there is no single licence across
`graphics/`, and no invariant that there should be:

| what | licence |
|---|---|
| DRM helpers (`drm_simple_kms_helper.c`, `drm_gem_vram_helper.c`, `drm_gem_atomic_helper.c`, `drm_gem_framebuffer_helper.c`), `tiny/bochs.c` | GPL-2.0-or-later |
| `vboxvideo/` | MIT |
| `drivers/gpu/drm/virtio/` | MIT (the X11-style Red Hat header), except `virtgpu_submit.c` which is SPDX MIT |
| `lib/genalloc.c`, `drm_gem_shmem_helper.c` | **GPL-2.0-only** |
| our own code — the virtio shim, every `*_freebsd.c` glue file, `virtgpu_trace.h` | BSD-2-Clause, as is the repo |

Two consequences worth stating plainly, because an earlier version of this file
asserted "or-later" as an invariant and the tree did not hold it —
`lib/genalloc.c` has been GPL-2.0-only and shipping inside `VBoxGraphics.kext`
since PR #32:

- **For shipping, this changes nothing.** The combined `.ko` is conveyed under
  GPL-2.0 either way, because it is vendored Linux; corresponding source is
  public; nothing here is GPLv3.
- **For upstreaming, it decides one thing.** FreeBSD base will not take GPL
  code into `sys/`, so a GPL-2.0-only helper can never go there. The virtio
  shim is deliberately BSD-2-Clause for exactly that reason: it is the piece
  that plausibly belongs in base, and keeping it clean costs nothing today.

`virtgpu_vram.c` is not vendored. It is the one GPL-2.0-only file in the
virtio-gpu driver, and it implements host-visible blob resources, which need a
`get_shm_region` bus method FreeBSD's virtio bus does not have — so the two
reasons to leave it out agree.

## Measured linuxkpi gaps (bochs)

From the CI build log, not estimated:

| gap | resolution |
|---|---|
| `pdev->resource[N].flags` | LinuxKPI's `struct pci_dev` has no `resource[]`; use `pci_resource_flags()`, which it does provide |
| `request_region()` for the legacy VBE ioports | absent from LinuxKPI. That path only runs on devices with no MMIO BAR; qemu stdvga, `-device bochs-display` and Simics all have one, so the FreeBSD build refuses such a device rather than driving unclaimed ports |
| `sysctl___hw_bochs` undeclared | `-DDRM_SYSCTL_PARAM_PREFIX=_bochs` needs a `SYSCTL_NODE(_hw, …, bochs)` **in the same TU as the module params** — bochs.c has `module_param_named(modeset, …)`, so it goes there, not in the glue file |
| `VGA_ATT_W` / `VGA_IS1_RC` / `VGA_MIS_COLOR` undeclared | FreeBSD's linuxkpi *does* ship `video/vga.h` (19 lines, `common/include/`), but only carries `VGA_MIS_W` of the four bochs needs. It wins the include search, so bochs.c supplements the missing three rather than shadowing it |
| `drm_device` has no `anon_inode` (**the VRAM helper's only gap**) | drm-kmod's `ttm_device_init()` takes `void *dummy` where Linux takes `struct address_space *`; `radeon_ttm.c` passes `NULL`, and the helper now does the same |

## Status

| piece | state |
|---|---|
| `drm_extra_helpers` | **shipping**, both arches (`IOGraphicsExtras.kext`) |
| `bochs` | **shipping**, amd64 — loads, binds `1234:1111` and drives the framebuffer in the CI boot guest |
| `vboxvideo` | **shipping**, amd64 — compile + kext packaging proven; attach proven on a real VirtualBox guest |
| `drm_shmem_helpers` | compile-only, non-gating, both arches |
| `linuxkpi_virtio` | compile-only, non-gating, both arches |
| `virtio_gpu_drm` | compile-only, non-gating, both arches |

The three new steps exist to *measure*, in the same shape the bochs and
vboxvideo steps started in: they print missing headers, undeclared identifiers,
incomplete types and undefined symbols from the build log, so the plan's
estimates become a task list. Nothing new is packaged as a kext yet.

virtio-gpu is the first driver here where the **arm64** leg is the point rather
than a bonus. qemu's arm64 `virt` machine has no VGA device for bochs to bind
(`ArmVirtQemu.dsc` ships `QemuRamfbDxe` and `VirtioGpuDxe`, not
`QemuVideoDxe`), and VirtualBox guests are x86 — so on arm64 this is the only
candidate for `/dev/dri/card0`. It matters equally on amd64, where a QEMU/KVM
guest gets the same driver and the existing boot test can actually exercise it.

What is **not** proven yet, in dependency order:

1. that the three modules compile at all (this PR measures it),
2. that they `kldload` — symbols resolve at load, not at link, so
   `tools/check-kext-symbols.py` is the gate once they are packaged,
3. that the newbus attach in `virtio_gpu_drm_freebsd.c` binds — in particular
   that `lkpinew_pci_dev()` on the virtio-pci parent gives the vendored code a
   `struct device` complete enough for `drm_dev_alloc()` and the
   `efifb` → `drmfb` handover,
4. that KMS comes up, and only then that VIRGL negotiates and Mesa's virgl
   binds.

Userspace needs no work for any of it: Mesa already builds the virgl Gallium
driver unconditionally on both architectures, and `libdrm` installs
`virtgpu_drm.h` on both.
