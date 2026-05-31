# nextbsd-kernel-modules

Builds NextBSD kernel modules layered on the kernel `obj` tree, inside the
SHA-pinned [`nextbsd-kernel-toolchain`](https://github.com/nextbsd-redux/nextbsd-kernel-toolchain)
container.

## Why a separate build?

Kernel modules are linked against the exact kernel config and source. A custom
kernel means stock `kmod` packages won't load reliably, so modules are built
from the same patched source the kernel was built from.

## Triggers

- `repository_dispatch: kernel-updated` — from `nextbsd-kernel` after a full
  (source-changing) kernel build. Carries `run_id` (to fetch the
  `kernel-obj-amd64` artifact), the pinned `image_tag`, and `fbsd_sha`.
- `workflow_dispatch` — manual.

Set `client_payload[release]=true` on the dispatch to also publish a
`tar.gz` of the modules to GitHub Releases.
