#!/bin/sh
# gen-i915-personalities.sh — emit an Apple IOKitPersonalities block for the
# Intel i915 (i915kms) DRM/KMS graphics driver, derived from the driver's own
# PCI id table.
#
# Graphics kext series (IntelGraphics.kext <- i915kms, drm-kmod). The companion
# to gen-iwlwifi-personalities.sh (#213) and gen-em-personalities.sh (#219):
# Apple kexts carry IOKitPersonalities in Info.plist whose IOPCIPrimaryMatch
# device-id list the in-kernel IOService matcher (K2/K3, #215/#216) reads to bind
# a driver to a device. We produce the faithful equivalent for i915kms by parsing
# the device ids straight out of the drm-kmod source being compiled.
#
# i915's ids live in a pciids header (drm-kmod: drivers/gpu/drm/i915/i915_pciids.h,
# upstream macro INTEL_VGA_DEVICE):
#
#     INTEL_VGA_DEVICE(0x5916, info)   ->   .vendor = 0x8086, .device = 0x5916
#
# grouped behind family macros (INTEL_KBL_IDS, INTEL_SKL_IDS, ...) that all
# expand to INTEL_VGA_DEVICE(0x...., info) rows, so grepping every
# INTEL_VGA_DEVICE(0x....) captures the whole match table. Every entry is Intel
# (0x8086). IOPCIPrimaryMatch wants the PCI id word as 0xDDDDVVVV (device in the
# high 16 bits, vendor low), e.g. device 0x5916 -> "0x59168086".
#
# Emitting from source (rather than parsing the compiled .ko's MODULE_PNP_INFO via
# kldxref, which isn't in the build image) keeps this robust and is exactly the
# data the in-kernel matcher consumes — and is only possible because we build
# drm-kmod from source (not from a pkg .ko, which carries no source to parse).
#
# Output (stdout) is a complete `<key>IOKitPersonalities</key><dict>...</dict>`
# block ready to drop into Info.plist via `ko2kext.sh -p`.
set -eu

usage() {
	echo "usage: gen-i915-personalities.sh <i915_pciids.h> <bundle-id> <ioclass>" >&2
	exit 1
}

[ $# -eq 3 ] || usage
SRC="$1"; BUNDLE_ID="$2"; IOCLASS="$3"
[ -f "$SRC" ] || { echo "gen-i915-personalities: no such file: $SRC" >&2; exit 1; }

VENDOR=8086   # PCI_VENDOR_ID_INTEL — every INTEL_VGA_DEVICE entry is Intel.

# Unique device ids (the 0x.... first arg of INTEL_VGA_DEVICE), lowercase, padded
# to 4 hex. Some headers also use INTEL_VGA_DEVICE without a literal id (rare,
# parametrised macros) — those simply don't match the 0x.... and are skipped.
IDS=$(grep -oE 'INTEL_VGA_DEVICE\(0x[0-9A-Fa-f]+' "$SRC" \
	| grep -oE '0x[0-9A-Fa-f]+' \
	| tr 'A-F' 'a-f' \
	| sort -u)
[ -n "$IDS" ] || { echo "gen-i915-personalities: no INTEL_VGA_DEVICE ids in $SRC" >&2; exit 1; }

# Build the space-separated IOPCIPrimaryMatch string: 0x<device><vendor>.
MATCH=""
N=0
for id in $IDS; do
	dev=${id#0x}
	while [ ${#dev} -lt 4 ]; do dev="0${dev}"; done
	MATCH="${MATCH}0x${dev}${VENDOR} "
	N=$((N + 1))
done
MATCH=${MATCH% }   # trim trailing space

# IOProbeScore: higher than the generic VGA/framebuffer nexus so the real KMS
# driver wins the match. Mirrors Apple kexts (and gen-iwlwifi/gen-em).
cat <<EOF
	<key>IOKitPersonalities</key>
	<dict>
		<key>${IOCLASS}</key>
		<dict>
			<key>CFBundleIdentifier</key>
			<string>${BUNDLE_ID}</string>
			<key>IOClass</key>
			<string>${IOCLASS}</string>
			<key>IOProviderClass</key>
			<string>IOPCIDevice</string>
			<key>IOPCIPrimaryMatch</key>
			<string>${MATCH}</string>
			<key>IOProbeScore</key>
			<integer>10000</integer>
		</dict>
	</dict>
EOF

echo "gen-i915-personalities: ${N} Intel i915 device ids from $(basename "$SRC")" >&2
