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
# DE-OVERLAP with amdgpu (-x): radeon and amdgpu OVERLAP on Southern Islands (SI)
# and Sea Islands (CIK) — those device ids appear in BOTH radeon_PCI_IDS and
# amdgpu's pciidlist (Linux/FreeBSD pick the driver via radeon.cik_support /
# amdgpu.cik_support). amdgpu is the modern default for those generations, so
# RadeonGraphics must drop any id AMDGraphics claims — otherwise a SI/CIK card
# matches BOTH kexts (an IOProbeScore tie / ambiguous bind). Pass amdgpu's match
# words (0xDDDD1002, one per line — e.g. extracted from the AMDGraphics
# IOPCIPrimaryMatch) via `-x <file>`; radeon omits exactly those. The rule is
# self-correcting on build config: whatever amdgpu ACTUALLY claims is excluded;
# any SI/CIK id amdgpu didn't build stays in radeon, so the card is still driven.
#
# Output (stdout) is a complete `<key>IOKitPersonalities</key><dict>...</dict>`
# block ready to drop into Info.plist via `ko2kext.sh -p`.
set -eu

usage() {
	echo "usage: gen-radeon-personalities.sh [-x exclude-file] <drm_pciids.h> <bundle-id> <ioclass>" >&2
	echo "  -x exclude-file  match words (0xDDDD1002, one per line) to omit — e.g. amdgpu's," >&2
	echo "                   so SI/CIK cards bind amdgpu, not radeon (repeatable not needed)" >&2
	echo "  <drm_pciids.h>   include/drm/drm_pciids.h (holds the radeon_PCI_IDS macro)" >&2
	exit 1
}

EXCLUDE_FILE=""
while getopts "x:h" opt; do
	case "$opt" in
	x) EXCLUDE_FILE="$OPTARG" ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))

[ $# -eq 3 ] || usage
SRC="$1"; BUNDLE_ID="$2"; IOCLASS="$3"
[ -f "$SRC" ] || { echo "gen-radeon-personalities: no such file: $SRC" >&2; exit 1; }

VENDOR=1002   # PCI_VENDOR_ID_ATI/AMD — every radeon_PCI_IDS row is ATI/AMD.

# De-overlap set: amdgpu's match words to omit from radeon (lowercased, deduped).
# Empty when no -x — then radeon emits its full table unchanged.
EXCLUDES=""
if [ -n "$EXCLUDE_FILE" ]; then
	[ -f "$EXCLUDE_FILE" ] || { echo "gen-radeon-personalities: exclude file not found: $EXCLUDE_FILE" >&2; exit 1; }
	EXCLUDES=$(grep -oE '0x[0-9A-Fa-f]{8}' "$EXCLUDE_FILE" | tr 'A-F' 'a-f' | sort -u)
fi

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

# Build the space-separated IOPCIPrimaryMatch string: 0x<device><vendor>,
# skipping any word amdgpu already claims (the SI/CIK de-overlap).
MATCH=""
N=0
NX=0
for id in $IDS; do
	dev=${id#0x}
	while [ ${#dev} -lt 4 ]; do dev="0${dev}"; done
	word="0x${dev}${VENDOR}"
	# Set membership via newline-delimited match (POSIX-portable).
	case "
${EXCLUDES}
" in
	*"
${word}
"*) NX=$((NX + 1)); continue ;;
	esac
	MATCH="${MATCH}${word} "
	N=$((N + 1))
done
MATCH=${MATCH% }   # trim trailing space
[ -n "$MATCH" ] || { echo "gen-radeon-personalities: every radeon id was excluded by -x — nothing left" >&2; exit 1; }

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

echo "gen-radeon-personalities: ${N} AMD/ATI radeon device ids from $(basename "$SRC") (excluded ${NX} overlapping amdgpu ids)" >&2
