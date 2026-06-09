#!/bin/sh
# gen-radeon-personalities.sh — emit an Apple IOKitPersonalities block for the
# AMD/ATI radeon (radeonkms) DRM/KMS graphics driver, derived from the driver's
# own PCI id table.
#
# Graphics kext series (RadeonGraphics.kext <- radeonkms, drm-kmod). Same approach
# as gen-iwlwifi/gen-em/gen-i915/gen-amdgpu: parse the device ids from the
# drm-kmod source. radeon's ids live in the radeon_PCI_IDS macro in
# include/drm/drm_pciids.h, which radeon_drv.c expands into its pciidlist[]:
#
#     #define radeon_PCI_IDS \
#         {0x1002, 0x3150, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RV380|RADEON_IS_MOBILITY}, \
#         {0x1002, 0x3151, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RV380|RADEON_IS_MOBILITY}, \
#         ...
#
# CRITICAL: drm_pciids.h also holds r128_PCI_IDS, mga_PCI_IDS, etc. — and r128
# (Rage 128) is ALSO vendor 0x1002 but is NOT driven by radeonkms. So we must
# scope to the radeon_PCI_IDS macro block, not grep the whole file for 0x1002.
#
# Every radeon row is AMD/ATI (vendor 0x1002), 2nd field = device id.
# IOPCIPrimaryMatch wants 0xDDDDVVVV (device high, vendor low), e.g.
# 0x3150 -> "0x31501002".
#
# Output (stdout) is a complete `<key>IOKitPersonalities</key><dict>...</dict>`
# block ready to drop into Info.plist via `ko2kext.sh -p`.
set -eu

usage() {
	echo "usage: gen-radeon-personalities.sh <drm_pciids.h> <bundle-id> <ioclass>" >&2
	echo "  <drm_pciids.h>  include/drm/drm_pciids.h (holds the radeon_PCI_IDS macro)" >&2
	exit 1
}

[ $# -eq 3 ] || usage
SRC="$1"; BUNDLE_ID="$2"; IOCLASS="$3"
[ -f "$SRC" ] || { echo "gen-radeon-personalities: no such file: $SRC" >&2; exit 1; }

VENDOR=1002   # PCI_VENDOR_ID_ATI/AMD — every radeon_PCI_IDS row is ATI/AMD.

# Extract the radeon_PCI_IDS macro block: a backslash-continued #define. Start at
# the `#define radeon_PCI_IDS` line, keep printing while lines end in a line
# continuation `\`, and stop on the first line that does not (the macro's end).
BLOCK=$(awk '
	/#define[ \t]+radeon_PCI_IDS/ { inblk = 1 }
	inblk { print }
	inblk && $0 !~ /\\[ \t]*$/ { exit }
' "$SRC")
[ -n "$BLOCK" ] || { echo "gen-radeon-personalities: radeon_PCI_IDS macro not found in $SRC" >&2; exit 1; }

# Device ids = 2nd hex field of each {0x1002, 0x....} row within that block only.
IDS=$(printf '%s\n' "$BLOCK" \
	| grep -oE '\{[[:space:]]*0x1002,[[:space:]]*0x[0-9A-Fa-f]+' \
	| grep -oE '0x[0-9A-Fa-f]+$' \
	| tr 'A-F' 'a-f' \
	| sort -u)
[ -n "$IDS" ] || { echo "gen-radeon-personalities: no {0x1002, 0x....} rows in radeon_PCI_IDS" >&2; exit 1; }

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
# driver wins the match. Mirrors Apple kexts (and the sibling generators).
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

echo "gen-radeon-personalities: ${N} AMD/ATI radeon device ids from $(basename "$SRC")" >&2
