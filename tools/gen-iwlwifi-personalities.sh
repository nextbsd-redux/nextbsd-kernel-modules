#!/bin/sh
# gen-iwlwifi-personalities.sh — emit an Apple IOKitPersonalities block for the
# Intel iwlwifi driver, derived from the driver's own PCI id table.
#
# P1 (#213). Apple kexts carry hand/script-maintained IOKitPersonalities in
# their Info.plist — IOPCIPrimaryMatch device-id lists the in-kernel IOService
# matcher (K2/K3, #215/#216) reads to bind a driver to a device. We produce the
# faithful equivalent for if_iwlwifi by parsing the device ids straight out of
# the driver source that's being compiled (sys/contrib/dev/iwlwifi/pcie/drv.c):
#
#     IWL_PCI_DEVICE(dev, subdev, cfg)  ->  .vendor = PCI_VENDOR_ID_INTEL (0x8086)
#                                           .device = dev
#
# so every entry is an Intel (0x8086) device. IOPCIPrimaryMatch wants the PCI id
# word as 0xDDDDVVVV (device in the high 16 bits, vendor in the low 16), e.g.
# device 0x24f3 -> "0x24f38086". Emitting from source (rather than parsing the
# compiled .ko's MODULE_PNP_INFO via kldxref, which isn't in the build image)
# keeps this robust and is exactly the data the in-kernel matcher consumes.
#
# Output (stdout) is a complete `<key>IOKitPersonalities</key><dict>...</dict>`
# block ready to drop into Info.plist via `ko2kext.sh -p`.
set -eu

usage() {
	echo "usage: gen-iwlwifi-personalities.sh <iwlwifi-drv.c> <bundle-id> <ioclass>" >&2
	exit 1
}

[ $# -eq 3 ] || usage
SRC="$1"; BUNDLE_ID="$2"; IOCLASS="$3"
[ -f "$SRC" ] || { echo "gen-iwlwifi-personalities: no such file: $SRC" >&2; exit 1; }

VENDOR=8086   # PCI_VENDOR_ID_INTEL — every IWL_PCI_DEVICE entry is Intel.

# Unique device ids (first IWL_PCI_DEVICE arg), normalised to lowercase 4-hex.
IDS=$(grep -oE 'IWL_PCI_DEVICE\(0x[0-9A-Fa-f]+' "$SRC" \
	| grep -oE '0x[0-9A-Fa-f]+' \
	| tr 'A-F' 'a-f' \
	| sort -u)
[ -n "$IDS" ] || { echo "gen-iwlwifi-personalities: no IWL_PCI_DEVICE ids in $SRC" >&2; exit 1; }

# Build the space-separated IOPCIPrimaryMatch string: 0x<device><vendor>.
MATCH=""
N=0
for id in $IDS; do
	dev=${id#0x}
	MATCH="${MATCH}0x${dev}${VENDOR} "
	N=$((N + 1))
done
MATCH=${MATCH% }   # trim trailing space

# IOProbeScore: higher than generic/legacy nexus drivers so a hardware match
# wins. Mirrors Apple kexts (typically 10000-ish for a concrete NIC driver).
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

echo "gen-iwlwifi-personalities: ${N} Intel device ids from $(basename "$SRC")" >&2
