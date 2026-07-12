#!/bin/sh
# nvidia-activate.sh — wire one installed NVIDIA<NNN>.bundle's userland into the
# canonical /usr/local paths so X (auto-detect), ld.so, GLVND, and Vulkan find it.
# The bundle is version-locked storage that co-exists per-branch on disk; this makes
# exactly one branch "live" by symlinking its entry points into the standard search
# locations. Idempotent (ln -sf). Ships inside every bundle (copied by nvidia-mkbundle.sh).
#
# Single-branch systems: run once at package-build/stage time (the symlinks ship in
# NextBSD-kernel-extensions). Multi-branch later: the install/boot activator calls this
# for whichever branch autoloaded (PCI-id/kext-keyed), swapping the canonical symlinks.
#
# The symlink TARGETS are the bundle's absolute final install path (arg 1), so they
# resolve after install regardless of any staging prefix (arg 2). Portable POSIX sh.
# usage: nvidia-activate.sh <installed-bundle-path> [dest-root]
#   <installed-bundle-path>  final abs path, e.g. /System/Library/Extensions/NVIDIA595.bundle
#   [dest-root]              optional prefix for where the /usr/local links are written
set -eu

[ $# -ge 1 ] || { echo "usage: nvidia-activate.sh <installed-bundle-path> [dest-root]" >&2; exit 1; }
BUNDLE="$1"                 # absolute FINAL location of the bundle (== symlink target base)
ROOT="${2:-}"              # optional staging prefix for where the links are created
# READ the resources from where this script actually lives (works at stage time,
# where the bundle sits under a staging prefix, and post-install alike); the symlink
# TARGET is always the final $BUNDLE path so links resolve after install.
SELF="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"   # .../<bundle>/Contents/Resources
L="$ROOT/usr/local"

# link every file/symlink under Resources/$1 into /usr/local/$2 (same basenames),
# each pointing at the final $BUNDLE/Contents/Resources/$1/<name>
link_dir() {
	_src="$SELF/$1"; _dst="$L/$2"
	[ -d "$_src" ] || return 0
	mkdir -p "$_dst"
	for _f in "$_src"/*; do
		[ -e "$_f" ] || [ -L "$_f" ] || continue
		# skip real subdirs (e.g. lib/gbm) — each has its own explicit link_dir call
		[ -d "$_f" ] && [ ! -L "$_f" ] && continue
		ln -sf "$BUNDLE/Contents/Resources/$1/$(basename "$_f")" "$_dst/$(basename "$_f")"
	done
}

link_dir xorg/drivers                     lib/xorg/modules/drivers
link_dir xorg/extensions                  lib/xorg/modules/extensions
link_dir lib                              lib
link_dir lib/gbm                          lib/gbm
link_dir share/glvnd/egl_vendor.d         share/glvnd/egl_vendor.d
link_dir share/egl/egl_external_platform.d share/egl/egl_external_platform.d
link_dir share/vulkan/icd.d               share/vulkan/icd.d
link_dir etc/libmap.d                     etc/libmap.d

echo "nvidia-activate: linked $(basename "$BUNDLE") into ${L}"
