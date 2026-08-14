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

So the plan's central finding holds at 6.12: *the cost of a small virtual-GPU
driver is the helper layer, not the driver*.

## Layout

    drivers/gpu/drm/   Linux v6.12 sources, vendored UNMODIFIED
    include/drm/       their headers, likewise
    drm_extra_helpers/ FreeBSD module: Makefile + kld glue

`drm_extra_helpers.ko` compiles the helpers **once** and exports them. Bundling
them per-driver would make two drivers collide on `EXPORT_SYMS`, which is why
the plan calls for a shared module.

Sources are GPL-2.0-or-later, as is the rest of the vendored Linux DRM code.

## Status

Compile-only, non-gating. The CI step exists to *measure* the linuxkpi gap
rather than to claim a working driver: it prints missing headers, undeclared
identifiers and incomplete types from the build log. That measured list is what
turns the plan's estimate into a task list.

Not yet done: `bochs.c` itself (needs these helpers first), the kext wrapper
(`ko2kext.sh` + a personality generator for `1234:1111`), and the end-to-end CI
test — the amd64 boot guest already carries the target device, since q35's
default VGA *is* stdvga.
