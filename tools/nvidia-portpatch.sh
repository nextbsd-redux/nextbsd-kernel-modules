#!/bin/sh
# nvidia-portpatch.sh — apply FreeBSD's x11/nvidia-kmod + graphics/nvidia-drm-kmod
# post-patch REINPLACE_CMD edits to an extracted NVIDIA-FreeBSD driver tree, so
# NVIDIA's source builds against a modern FreeBSD/NextBSD kernel.
#
# WHY THIS EXISTS: the FreeBSD ports fix NVIDIA's source with a long list of
# in-place ${REINPLACE_CMD} edits in the port Makefiles (NOT patch files). We are
# not on a FreeBSD host with the ports framework, so we transcribe those edits
# 1:1 here. Each edit below cites the port file + guard it comes from. This is
# nvidia#384 (build) — mirror of how drm-kmod vendored its gen-*.sh helpers.
#
# Transcribed for driver 595.84 (NVVERSION 595.084, NVVERSION:R 595), amd64,
# default port options ACPI_PM+LINUX, against a NextBSD releng/15.1 base
# (__FreeBSD_version >= 1500051, OSVERSION-gated edits fire). SUB_PATCHES (the
# .in unified diffs) are applied by the caller BEFORE this runs.
#
# Portable sed only (runs under both the CI Linux GNU sed and BSD sed): no `\t`,
# no `+N` relative addressing, no `[[:<:]]`; edits go through a temp file (no
# `sed -i` portability split); every edit is change-checked. Load-bearing edits
# are `must` (fail the build if they don't match — catches source drift on a
# future bump); the rest are `opt` (no-op-safe: the port applies them harmlessly
# to versions/bases that still carry the old text; 595.84 may already be fixed).
set -eu

[ $# -ge 1 ] || { echo "usage: nvidia-portpatch.sh <WRKSRC> [NVSRC]" >&2; exit 1; }
W="$1"                      # extracted NVIDIA-FreeBSD-x86_64-<ver> dir (== WRKSRC)
NVSRC="${2:-nvidia}"        # src subdir (nvidia; '.' only for the userland port)
[ -d "$W/src/$NVSRC" ] || { echo "nvidia-portpatch: no $W/src/$NVSRC" >&2; exit 1; }
TAB=$(printf '\t')

# ed <label> <must|opt> <file> <sed-args...> — apply one sed, assert it changed
# when `must`. Uses a temp file so it is -i-portable across GNU/BSD sed.
ed() {
	_l="$1"; _req="$2"; _f="$3"; shift 3
	[ -f "$_f" ] || { [ "$_req" = must ] && { echo "FAIL: missing $_f ($_l)" >&2; exit 1; }; echo "  skip(absent): $_l"; return; }
	_b=$(cksum < "$_f")
	sed "$@" "$_f" > "$_f.nvpp" && mv "$_f.nvpp" "$_f"
	if [ "$_b" = "$(cksum < "$_f")" ]; then
		[ "$_req" = must ] && { echo "FAIL: no change — $_l ($_f)" >&2; exit 1; }
		echo "  opt no-op: $_l"
	else echo "  ok: $_l"; fi
}

S="$W/src/$NVSRC"
MSET="$W/src/nvidia-modeset"
echo "== nvidia-kmod post-patch (x11/nvidia-kmod/Makefile) =="

# SUBDIR trim: build only kmods. Port: s/SUBDIR=\tsrc \\/SUBDIR=\tsrc/ ; /lib/,/doc/d
ed "SUBDIR strip trailing continuation" must "$W/Makefile" -e "/^SUBDIR=${TAB}src /s/ \\\\\$//"
ed "SUBDIR delete lib..doc"              must "$W/Makefile" -e "/${TAB}lib /,/${TAB}doc\$/d"
# Port: /.if exists(nvml)/,/.endif/d
ed "SUBDIR delete nvml block"            must "$W/Makefile" -e "/\.if exists(nvml)/,/\.endif/d"

# -CURRENT check: delete first `#if __FreeBSD_version` + 2 lines. Port computes
# the line number then deletes N,+2 — do it portably (no `+2`).
NVF="$S/nv-freebsd.h"
ln=$(sed -ne '/^#if __FreeBSD_version/{=;q;}' "$NVF" || true)
if [ -n "${ln:-}" ]; then
	end=$((ln + 2))
	ed "nv-freebsd.h drop -CURRENT guard" must "$NVF" -e "${ln},${end}d"
else echo "FAIL: no '#if __FreeBSD_version' in nv-freebsd.h" >&2; exit 1; fi

# Linux headers after src r246085 (opt: 595 may already be correct)
ed "nvidia_linux.c linux_ioctl.h include" opt "$S/nvidia_linux.c" \
	-E -e '/#include "machine\/\.\.\/linux(32)?\/linux.h"/{x;s/.*/#include "machine\/..\/..\/compat\/linux\/linux_ioctl.h"/;H;x;}'
# memset fix (opt)
ed "nvidia_subr.c memset sizeof(*ci)" opt "$S/nvidia_subr.c" -e '/memset/s/sizeof(ci/sizeof(*ci/'
# sys/capability.h -> sys/capsicum.h (opt: 595 nv-freebsd.h already includes capsicum)
ed "nv-freebsd.h capsicum rename" opt "$NVF" -e 's:sys/capability\.h:sys/capsicum.h:'
ed "modeset-freebsd.c capsicum rename" opt "$MSET/nvidia-modeset-freebsd.c" -e 's:sys/capability\.h:sys/capsicum.h:'
# lock.h -> mutex.h (>=358)
ed "modeset-freebsd.c lock.h->mutex.h" opt "$MSET/nvidia-modeset-freebsd.c" -e '/^#include/s:lock\.h:mutex.h:'
# sys/module.h after sys/param.h (>=358): delete the param.h include, then before
# the module.h include re-insert param.h then a blank/hold (port uses x;s;G).
ed "modeset-freebsd.c param.h before module.h" opt "$MSET/nvidia-modeset-freebsd.c" \
	-e '/^#include <sys\/param\.h>/d' \
	-e '/^#include <sys\/module\.h>/{x;s:^:#include <sys/param.h>:;G;}'
# don't auto-register / install (opt cosmetic for a build-only job, but faithful)
ed "Makefile afterinstall guard" opt "$W/Makefile" -e 's/afterinstall/&_dontexecute/'
ed "src Makefile beforeinstall guard" opt "$S/Makefile" -e 's/beforeinstall/&_dontexecute/'
# DRIVER_MODULE drops nvidia_devclass (opt: 595 already omits it)
ed "nvidia_pci.c drop nvidia_devclass" opt "$S/nvidia_pci.c" -e '/^DRIVER_MODULE/s/, nvidia_devclass//'
# clang-21 __printf__ (opt at clang-19; keep faithful). Note the SPACE in 'format ('
ed "modeset printf attr 1" opt "$MSET/nvidia-modeset-os-interface.h" -e 's/format (printf, 3, 4)/format (__printf__, 3, 4)/'
ed "modeset printf attr 2" opt "$MSET/nvidia-modeset-os-interface.h" -e 's/va_list ap);/va_list ap); __attribute__((format (__printf__, 3, 0)));/'
# K&R prototype (opt)
ed "nvlink_freebsd.c K&R void" opt "$S/nvlink_freebsd.c" -e '/nvlink_allocLock/s,(),(void),'
# default option ACPI_PM on -> define NV_SUPPORT_ACPI_PM
ed "nv-freebsd.h enable ACPI_PM" opt "$NVF" -E -e 's/undef (NV_SUPPORT_ACPI_PM)/define \1/'
# DMAP_* removed on 15.x (OSVERSION >= 1500051) -> kva_layout.* (must: 15.1 needs it)
ed "nvidia_subr.c DMAP->kva_layout" must "$S/nvidia_subr.c" \
	-e 's/DMAP_MIN_ADDRESS/kva_layout.dmap_low/' \
	-e 's/DMAP_MAX_ADDRESS/kva_layout.dmap_high/'

# ---- nvidia-drm post-patch (graphics/nvidia-drm-kmod/Makefile.common) ----
# Only run when the drm source is present AND requested (core-only builds skip).
DRM="$W/src/nvidia-drm"
if [ "${NVPP_DRM:-0}" = 1 ] && [ -d "$DRM" ]; then
	echo "== nvidia-drm post-patch (graphics/nvidia-drm-kmod/Makefile.common) =="
	# clang-19 fix (NOT in the port — needed because clang>=16 makes implicit
	# function/int declarations hard errors by default). NVIDIA's "functions"
	# conftests are inverted: a MISSING function must COMPILE (as an implicit decl)
	# to be ruled absent. NV_CONFTEST_CFLAGS lacks -Wno-implicit-function-declaration
	# (it's only in conftest.sh's build_cflags, which the FreeBSD compile_tests path
	# doesn't use), so every removed-in-6.12 function reads as PRESENT and nvidia-drm
	# calls pre-4.19 names that no longer exist. Add the same suppressions build_cflags
	# uses so the conftest logic resolves correctly against drm-612.
	ed "drm conftest allow implicit decls (clang-19)" must "$DRM/Makefile" \
		-e 's/NV_CONFTEST_CFLAGS += -Wno-error=redundant-decls/& -Wno-implicit-function-declaration -Wno-implicit-int -Wno-strict-prototypes/'
	# delete only the DRMKMODDIR dummy/include -I line (anchored — do NOT match the
	# always-active sys/compat continuation line)
	ed "drm Makefile drop dummy/include" opt "$DRM/Makefile" -e '/DRMKMODDIR.*\/linuxkpi\/dummy\/include/d'
	# fbdev off (>=570): two distinct seds on two distinct files
	ed "drm fbdev module_param arg 0" opt "$DRM/nvidia-drm-freebsd-lkpi.c" -e 's:&nv_drm_fbdev_module_param,  1,:\&nv_drm_fbdev_module_param,  0,:'
	ed "drm fbdev default false" opt "$DRM/nvidia-drm-os-interface.c" -e 's:bool nv_drm_fbdev_module_param = true;:bool nv_drm_fbdev_module_param = false;:'
	# >=575 const-cast fix (must if the source still has the non-const form)
	ed "drm l_info const-cast" opt "$DRM/nvidia-drm-drv.c" -e 's:struct nv_drm_mst_display_info \*l_info = (struct nv_drm_mst_display_info:const struct nv_drm_mst_display_info *l_info = (const struct nv_drm_mst_display_info:'
	ed "drm r_info const-cast" opt "$DRM/nvidia-drm-drv.c" -e 's:struct nv_drm_mst_display_info \*r_info = (struct nv_drm_mst_display_info:const struct nv_drm_mst_display_info *r_info = (const struct nv_drm_mst_display_info:'
	# pm_vt_switch_required guard-wrap (must: bases without the stub fail to link)
	ed "drm pm_vt_switch guard" must "$DRM/nvidia-drm-drv.c" \
		-e 's/.*pm_vt_switch_required(dev->dev, true);.*/#if __FreeBSD_version >= 1403507 \&\& __FreeBSD_version < 1500000 || __FreeBSD_version >= 1500504\n&\n#endif/'
fi

echo "nvidia-portpatch: done"
