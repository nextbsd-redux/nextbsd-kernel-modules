#!/bin/sh
# gen-bochs-personalities.sh — emit the IOKitPersonalities block for BochsGraphics
# from bochs.c's own pci_device_id table.
#
#   usage: gen-bochs-personalities.sh <bochs.c> <bundle-id> <IOClass>
#
# Parses the table rather than hardcoding, so a driver bump that adds or drops a
# device is picked up the same way gen-i915/amdgpu/radeon work. The table is
# tiny (three entries, two distinct vendor:device pairs), so unlike i915 there is
# no count-band gate — instead we assert the qemu stdvga id is present, since
# that is the one every CI guest matches on (q35's default -vga std).
#
# Match words are IOKit's device<<16|vendor form:
#   1234:1111  -> 0x11111234   Bochs VBE / qemu stdvga  (also -device bochs-display)
#   4321:1111  -> 0x11114321   Simics
#
# Deliberately NOT included: qxl (1b36:0100). qxl is a different device driven by
# the qxl driver; matching it here would bind bochs to hardware it cannot drive.
set -eu

SRC=${1:?usage: gen-bochs-personalities.sh <bochs.c> <bundle-id> <IOClass>}
BUNDLE_ID=${2:?missing bundle id}
IOCLASS=${3:?missing IOClass}

[ -f "$SRC" ] || { echo "gen-bochs-personalities: no such file: $SRC" >&2; exit 1; }

# Pull .vendor/.device pairs out of the bochs_pci_tbl[] initialiser.
# awk extracts the vendor/device literals as strings; the hex arithmetic happens
# in the shell, because strtonum() is a gawk extension that neither mawk
# (Ubuntu's default awk) nor BSD awk implements.
PAIRS=$(awk '
	/bochs_pci_tbl\[\] *= *\{/ { in_tbl = 1; next }
	in_tbl && /^};/            { exit }
	in_tbl && /\.vendor/       { if (match($0, /0x[0-9a-fA-F]+/)) v = substr($0, RSTART, RLENGTH) }
	in_tbl && /\.device/       {
		if (match($0, /0x[0-9a-fA-F]+/)) {
			d = substr($0, RSTART, RLENGTH)
			if (v != "" && d != "") { print v " " d; v = ""; d = "" }
		}
	}
' "$SRC")

MATCH=$(
	echo "$PAIRS" | while read -r v d; do
		[ -n "$v" ] || continue
		printf '0x%04x%04x\n' "$((d))" "$((v))"
	done | sort -u | tr '\n' ' ' | sed 's/ *$//'
)

[ -n "$MATCH" ] || { echo "gen-bochs-personalities: no ids parsed from $SRC" >&2; exit 1; }
case " $MATCH " in
*" 0x11111234 "*) ;;
*) echo "gen-bochs-personalities: qemu stdvga id 0x11111234 missing — table parse broke" >&2; exit 1 ;;
esac

cat <<PLIST
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
PLIST

N=$(printf '%s\n' $MATCH | wc -l | tr -d ' ')
echo "gen-bochs-personalities: ${N} device id(s) from $(basename "$SRC"): ${MATCH}" >&2
