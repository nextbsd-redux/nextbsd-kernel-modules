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
| `drm_gem_shmem_helper.c` | **missing** (virtio-gpu needs it) |
| `drm_gem_framebuffer_helper.c` | **a 76-line stub** — implements 3 of the functions its own header declares; `drm_gem_fb_create` is absent |

So the plan's central finding holds at 6.12: *the cost of a small virtual-GPU
driver is the helper layer, not the driver*.

## Layout

    drivers/gpu/drm/   Linux v6.12 sources; the helpers are verbatim, bochs.c
                       carries FreeBSD deltas in #ifdef __FreeBSD__ blocks --
                       the same convention drm-kmod uses for its own vendored
                       Linux sources
    include/drm/       their headers, verbatim
    drm_extra_helpers/ FreeBSD module: Makefile + kld glue

`drm_extra_helpers.ko` compiles the helpers **once** and exports them. Bundling
them per-driver would make two drivers collide on `EXPORT_SYMS`, which is why
the plan calls for a shared module.

Sources are GPL-2.0-or-later, as is the rest of the vendored Linux DRM code.

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

Compile-only, non-gating. The CI step exists to *measure* the linuxkpi gap
rather than to claim a working driver: it prints missing headers, undeclared
identifiers and incomplete types from the build log. That measured list is what
turns the plan's estimate into a task list.

Not yet done: `bochs.c` itself (needs these helpers first), the kext wrapper
(`ko2kext.sh` + a personality generator for `1234:1111`), and the end-to-end CI
test — the amd64 boot guest already carries the target device, since q35's
default VGA *is* stdvga.
