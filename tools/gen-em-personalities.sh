#!/bin/sh
# gen-em-personalities.sh — emit an Apple IOKitPersonalities block for the Intel
# e1000 ethernet driver (FreeBSD if_em.ko), derived from the driver's own PCI id
# tables.
#
# #219 (D1 — em → IntelEthernet.kext). The companion to gen-iwlwifi-personalities.sh
# (#213): Apple kexts carry IOKitPersonalities in Info.plist whose
# IOPCIPrimaryMatch device-id list the in-kernel IOService matcher (K2/K3,
# #215/#216) reads to bind a driver to a device. We produce the faithful
# equivalent for if_em by parsing the device ids straight out of the driver
# source being compiled.
#
# Unlike iwlwifi (literal hex in IWL_PCI_DEVICE(0x24f3, ...)), the e1000 probe
# tables use *symbolic* device ids:
#
#     sys/dev/e1000/if_em.c:
#       em_vendor_info_array[]  / igb_vendor_info_array[]
#         PVID(0x8086, E1000_DEV_ID_82540EM, "...")   <- symbol, not a literal
#
# and the symbols are #defined in sys/dev/e1000/e1000_hw.h:
#       #define E1000_DEV_ID_82540EM   0x100E
#
# So we (1) pull every E1000_DEV_ID_* symbol used in a PVID(0x8086, ...) row from
# both arrays (em == PRO/1000 GbE, igb == i210/i350 — both live in if_em.ko), and
# (2) resolve each to its hex value from e1000_hw.h. Every entry is Intel
# (0x8086). IOPCIPrimaryMatch wants the PCI id word as 0xDDDDVVVV (device high,
# vendor low), e.g. 0x100E -> "0x100e8086".
#
# Emitting from source (rather than the compiled .ko's MODULE_PNP_INFO via
# kldxref, which isn't in the build image) keeps this robust and is exactly the
# data the in-kernel matcher consumes.
#
# Output (stdout) is a complete `<key>IOKitPersonalities</key><dict>...</dict>`
# block ready to drop into Info.plist via `ko2kext.sh -p`.
set -eu

usage() {
	echo "usage: gen-em-personalities.sh <e1000-src-dir> <bundle-id> <ioclass>" >&2
	echo "  <e1000-src-dir>  sys/dev/e1000 (holds if_em.c + e1000_hw.h)" >&2
	exit 1
}

[ $# -eq 3 ] || usage
DIR="$1"; BUNDLE_ID="$2"; IOCLASS="$3"
SRC="$DIR/if_em.c"
HDR="$DIR/e1000_hw.h"
[ -f "$SRC" ] || { echo "gen-em-personalities: no such file: $SRC" >&2; exit 1; }
[ -f "$HDR" ] || { echo "gen-em-personalities: no such file: $HDR" >&2; exit 1; }

VENDOR=8086   # PCI_VENDOR_ID_INTEL — every PVID row in if_em.c is Intel.

# (1) Device-id symbols used in PVID(0x8086, <SYM>, ...) rows (both em + igb).
SYMS=$(grep -oE 'PVID\(0x8086,[[:space:]]*E1000_DEV_ID_[A-Za-z0-9_]+' "$SRC" \
	| grep -oE 'E1000_DEV_ID_[A-Za-z0-9_]+' \
	| sort -u)
[ -n "$SYMS" ] || { echo "gen-em-personalities: no PVID(0x8086, E1000_DEV_ID_*) rows in $SRC" >&2; exit 1; }

# (2) Resolve each symbol to its hex value from e1000_hw.h and build the
# space-separated IOPCIPrimaryMatch string: 0x<device><vendor>, deduped.
MATCH_IDS=""
for sym in $SYMS; do
	hex=$(grep -E "^#define[[:space:]]+${sym}[[:space:]]+0x[0-9A-Fa-f]+" "$HDR" \
		| grep -oE '0x[0-9A-Fa-f]+' | head -1)
	[ -n "$hex" ] || continue   # symbol with no numeric #define — skip
	dev=$(printf '%s' "${hex#0x}" | tr 'A-F' 'a-f')
	# Normalise to 4 hex digits (e1000 device ids are 16-bit).
	while [ ${#dev} -lt 4 ]; do dev="0${dev}"; done
	MATCH_IDS="${MATCH_IDS}0x${dev}${VENDOR}
"
done
MATCH=$(printf '%s' "$MATCH_IDS" | grep . | sort -u | tr '\n' ' ')
MATCH=${MATCH% }   # trim trailing space
[ -n "$MATCH" ] || { echo "gen-em-personalities: no device ids resolved from $HDR" >&2; exit 1; }
N=$(printf '%s\n' $MATCH | grep -c .)

# IOProbeScore: higher than generic/legacy nexus drivers so a hardware match
# wins. Mirrors Apple kexts (and gen-iwlwifi-personalities.sh).
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

# Sanity: qemu's CI NIC is the 82540EM (0x100e) — warn loudly if it dropped out,
# since the #219 boot test depends on it binding.
case " $MATCH " in
	*" 0x100e8086 "*) : ;;
	*) echo "gen-em-personalities: WARNING — 82540EM (0x100e8086, qemu CI NIC) not in match table" >&2 ;;
esac

echo "gen-em-personalities: ${N} Intel e1000 device ids from $(basename "$SRC")" >&2
