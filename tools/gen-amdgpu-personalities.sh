#!/bin/sh
# gen-amdgpu-personalities.sh — emit an Apple IOKitPersonalities block for the
# AMD amdgpu DRM/KMS graphics driver, derived from the driver's own PCI id table.
#
# Graphics kext series (AMDGraphics.kext <- amdgpu, drm-kmod). Same approach as
# gen-iwlwifi/gen-em/gen-i915: parse the device ids straight out of the drm-kmod
# source being compiled. amdgpu lists them as explicit pci_device_id rows in
# drivers/gpu/drm/amd/amdgpu/amdgpu_drv.c:
#
#     static const struct pci_device_id pciidlist[] = {
#         {0x1002, 0x6780, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
#         {0x1002, 0x6784, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
#         ...
#         {0, 0, 0}
#     };
#
# so every populated row is AMD (vendor 0x1002) and the SECOND field is the
# device id. We grep every {0x1002, 0x....} row and take the device id.
# IOPCIPrimaryMatch wants the PCI id word as 0xDDDDVVVV (device high, vendor low),
# e.g. 0x6780 -> "0x67801002".
#
# Output (stdout) is a complete `<key>IOKitPersonalities</key><dict>...</dict>`
# block ready to drop into Info.plist via `ko2kext.sh -p`.
set -eu

usage() {
	echo "usage: gen-amdgpu-personalities.sh <amdgpu_drv.c> <bundle-id> <ioclass>" >&2
	exit 1
}

[ $# -eq 3 ] || usage
SRC="$1"; BUNDLE_ID="$2"; IOCLASS="$3"
[ -f "$SRC" ] || { echo "gen-amdgpu-personalities: no such file: $SRC" >&2; exit 1; }

VENDOR=1002   # PCI_VENDOR_ID_ATI/AMD — every amdgpu pciidlist row is AMD.

# Device ids = the 2nd hex field of each {0x1002, 0x....} pci_device_id row.
# The first grep keeps "{0x1002, 0x<dev>"; the second pulls the trailing id.
IDS=$(grep -oE '\{[[:space:]]*0x1002,[[:space:]]*0x[0-9A-Fa-f]+' "$SRC" \
	| grep -oE '0x[0-9A-Fa-f]+$' \
	| tr 'A-F' 'a-f' \
	| sort -u)
[ -n "$IDS" ] || { echo "gen-amdgpu-personalities: no {0x1002, 0x....} rows in $SRC" >&2; exit 1; }

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
# driver wins the match. Mirrors Apple kexts (and gen-iwlwifi/gen-em/gen-i915).
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

echo "gen-amdgpu-personalities: ${N} AMD amdgpu device ids from $(basename "$SRC")" >&2
