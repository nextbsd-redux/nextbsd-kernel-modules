#!/bin/sh
# nvidia-mkbundle.sh — stage NVIDIA's precompiled X11/GL/EGL userland out of the
# NVIDIA-FreeBSD driver tarball into an Apple-style, per-branch, version-locked
# sibling bundle: NVIDIA<NNN>.bundle. Companion to the kernel side that ko2kext.sh
# wraps (NVIDIACore/Modeset/Graphics<NNN>.kext). A GPU driver is two layers — this
# is the userland half. See pkgdemon plan nextbsd-nvidia-userland-display-plan.html.
#
# WHY A BUNDLE (not /usr/local like the FreeBSD port): each NVIDIA branch's userland
# is version-locked to its kernel module and its fixed-name entry points collide
# (nvidia_drv.so, the .so.0/.so.1 sonames, the vendor JSONs). Giving each branch its
# own bundle dir makes them co-exist on disk (like the kexts); nvidia-activate.sh then
# wires exactly one branch into the canonical /usr/local paths so X/ld.so/GLVND find it.
#
# NO COMPILE: the userland ships as precompiled blobs in the tarball's obj/ tree
# (verified 595.84: obj/ holds the 64-bit sonamed .so files as real files, plus the
# NVIDIA-shipped vendor JSONs obj/10_nvidia.json / obj/15_nvidia_gbm.json /
# obj/10_nvidia_wayland.json, and root nvidia_icd.json). This is pure copy + generate.
#
# Repeatable for every branch: same invocation against that branch's tarball →
# NVIDIA580.bundle / NVIDIA470.bundle / NVIDIA390.bundle, each keyed to its kext.
# Portable POSIX sh (runs on the Linux CI runner). amd64 (lib/, not lib32/).
set -eu

[ $# -ge 3 ] || { echo "usage: nvidia-mkbundle.sh <WRKSRC> <version> <outdir>" >&2; exit 1; }
WRKSRC="$1"          # extracted NVIDIA-FreeBSD-x86_64-<ver> dir
VER="$2"             # e.g. 595.84
OUT="$3"            # dir to drop NVIDIA<NNN>.bundle into (== the nvidia-kexts dir)
BR="${VER%%.*}"      # 595.84 -> 595 (branch number, matches NVIDIAGraphics<BR>.kext)
OBJ="$WRKSRC/obj"
SELF_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

[ -f "$OBJ/nvidia_drv.so" ] || { echo "FAIL: $OBJ/nvidia_drv.so absent — is this a driver tarball?" >&2; exit 1; }

B="$OUT/NVIDIA${BR}.bundle"
R="$B/Contents/Resources"
rm -rf "$B"
mkdir -p "$R/xorg/drivers" "$R/xorg/extensions" "$R/lib/gbm" \
         "$R/share/glvnd/egl_vendor.d" "$R/share/egl/egl_external_platform.d" \
         "$R/share/vulkan/icd.d" "$R/etc/libmap.d"

# cp1 <src> <destdir> — copy one required file, fail loudly if missing
cp1() { [ -f "$1" ] || { echo "FAIL: missing $1" >&2; exit 1; }; cp "$1" "$2"; }
# cpopt <src> <destdir> — copy if present (optional pieces vary across branches)
cpopt() { [ -f "$1" ] && cp "$1" "$2" || echo "  skip(absent): $(basename "$1")"; }

echo "== NVIDIA${BR}.bundle userland ($VER) =="

# --- X DDX + server-side GLX (Resources/xorg) ---
cp1 "$OBJ/nvidia_drv.so"              "$R/xorg/drivers/"
cp1 "$OBJ/libglxserver_nvidia.so.1"  "$R/xorg/extensions/"
ln -sf libglxserver_nvidia.so.1 "$R/xorg/extensions/libglxserver_nvidia.so"

# --- client libs (Resources/lib): copy each soname (real files in obj/), add .so dev link ---
# The set must be DT_NEEDED-CLOSED: any libnvidia-* our own binaries link against but
# we fail to ship becomes a pkg(8) `shlibs_required` with no provider, and pkg resolves
# it by pulling in FreeBSD's nvidia-driver as a hard dependency (which is exactly what
# we're replacing). libnvidia-gpucomp is needed by glcore/eglcore — do not drop it.
# The closure self-check below enforces this.
for so in libGLX_nvidia.so.0 libEGL_nvidia.so.0 \
          libnvidia-glcore.so.1 libnvidia-glsi.so.1 libnvidia-cfg.so.1 libnvidia-tls.so.1 \
          libnvidia-eglcore.so.1 libnvidia-glvkspirv.so.1 libnvidia-gpucomp.so.1 \
          libnvidia-egl-gbm.so.1 libnvidia-allocator.so.1 \
          libnvidia-ml.so.1 libnvidia-vulkan-producer.so.1; do
  if [ -f "$OBJ/$so" ]; then
    cp "$OBJ/$so" "$R/lib/$so"
    ln -sf "$so" "$R/lib/${so%.so.*}.so"           # libFoo.so -> libFoo.so.N
  else echo "  skip(absent): $so"; fi
done
# Mesa's libgbm dlopen's lib<drmdriver>_gbm.so — for nvidia-drm that's the allocator
ln -sf ../libnvidia-allocator.so.1 "$R/lib/gbm/nvidia-drm_gbm.so"

# --- closure self-check: every NVIDIA lib our own binaries NEED must be in the bundle ---
# Guards the bug class above (a dangling NEEDED silently turns into a hard dep on
# FreeBSD's nvidia-driver at pkg-install time). Fails the build loudly instead.
if command -v readelf >/dev/null 2>&1; then
  MISS=""
  for f in "$R"/xorg/drivers/*.so "$R"/xorg/extensions/*.so.1 "$R"/lib/*.so.*; do
    [ -f "$f" ] || continue
    for n in $(readelf -d "$f" 2>/dev/null | sed -n 's/.*NEEDED.*\[\(lib[^]]*\)\].*/\1/p'); do
      case "$n" in
        *nvidia*) [ -e "$R/lib/$n" ] || MISS="$MISS $(basename "$f")->$n" ;;
      esac
    done
  done
  if [ -n "$MISS" ]; then
    echo "FAIL: bundle missing NVIDIA libs its own binaries NEED:$MISS" >&2
    echo "  pkg(8) would resolve these to FreeBSD's nvidia-driver and pull it in as a dep" >&2
    exit 1
  fi
  echo "  NVIDIA lib closure OK (no dangling NEEDED)"
else
  echo "  WARN: no readelf — skipping NVIDIA lib closure check" >&2
fi

# --- GLVND / EGL-external-platform / Vulkan vendor JSONs (NVIDIA-shipped) ---
cp1   "$OBJ/10_nvidia.json"          "$R/share/glvnd/egl_vendor.d/"
cpopt "$OBJ/15_nvidia_gbm.json"      "$R/share/egl/egl_external_platform.d/"
cpopt "$OBJ/10_nvidia_wayland.json"  "$R/share/egl/egl_external_platform.d/"
cpopt "$WRKSRC/nvidia_icd.json"      "$R/share/vulkan/icd.d/"

# --- libmap (GLVND GLX redirection, port-style) ---
printf 'libGLX_indirect.so.0\tlibGLX_nvidia.so.0\n' > "$R/etc/libmap.d/nvidia.conf"

# --- the reusable activator, shipped inside the bundle ---
cp1 "$SELF_DIR/nvidia-activate.sh" "$R/"
chmod +x "$R/nvidia-activate.sh"

# --- Info.plist (pairs the userland to its kext family) ---
cat > "$B/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleIdentifier</key>	<string>org.nextbsd.driver.nvidia${BR}userland</string>
	<key>CFBundleName</key>	<string>NVIDIA${BR}</string>
	<key>CFBundlePackageType</key>	<string>BNDL</string>
	<key>CFBundleShortVersionString</key>	<string>${VER}</string>
	<key>CFBundleVersion</key>	<string>${VER}</string>
	<key>NBSDUserlandForKext</key>	<string>org.nextbsd.driver.nvidiagraphics${BR}</string>
</dict>
</plist>
PLIST

echo "nvidia-mkbundle: staged $B ($(du -sh "$B" | cut -f1))"
find "$B" -type f -o -type l | sed "s|$B|  NVIDIA${BR}.bundle|" | sort
