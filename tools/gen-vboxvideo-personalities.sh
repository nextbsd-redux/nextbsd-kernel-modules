#!/bin/sh
# gen-vboxvideo-personalities.sh — emit the IOKitPersonalities block for
# VBoxGraphics from vbox_drv.c's own pci_device_id table.
#
#   usage: gen-vboxvideo-personalities.sh <vbox_drv.c> <bundle-id> <IOClass>
#
# Parses the table rather than hardcoding, matching how gen-i915/amdgpu/radeon/
# bochs work. vboxvideo's table uses the PCI_DEVICE(vendor, device) form rather
# than bochs' .vendor/.device designators, so the extraction differs, but the
# output shape is identical.
#
# The table has exactly one entry:
#   80ee:beef  -> 0xbeef80ee   VirtualBox Graphics Adapter
#
# One id means no count-band gate is meaningful; instead we assert that id is
# present, which catches a table parse silently returning nothing.
set -eu

SRC=${1:?usage: gen-vboxvideo-personalities.sh <vbox_drv.c> <bundle-id> <IOClass>}
BUNDLE_ID=${2:?missing bundle id}
IOCLASS=${3:?missing IOClass}

[ -f "$SRC" ] || { echo "gen-vboxvideo-personalities: no such file: $SRC" >&2; exit 1; }

# Pull vendor/device literals out of the pciidlist[] initialiser. As with the
# bochs generator, awk extracts the hex as strings and the shell does the
# arithmetic — strtonum() is a gawk extension that neither mawk (Ubuntu's
# default awk) nor BSD awk implements.
PAIRS=$(awk '
	/pciidlist\[\] *= *\{/ { in_tbl = 1; next }
	in_tbl && /^};/        { exit }
	in_tbl && /PCI_DEVICE\(/ {
		if (match($0, /PCI_DEVICE\([^)]*\)/)) {
			s = substr($0, RSTART, RLENGTH)
			n = split(s, parts, /[(,) ]+/)
			v = ""; d = ""
			for (i = 1; i <= n; i++)
				if (parts[i] ~ /^0[xX][0-9a-fA-F]+$/) {
					if (v == "") v = parts[i]
					else if (d == "") d = parts[i]
				}
			if (v != "" && d != "") print v " " d
		}
	}
' "$SRC")

MATCH=$(
	echo "$PAIRS" | while read -r v d; do
		[ -n "$v" ] || continue
		printf '0x%04x%04x\n' "$((d))" "$((v))"
	done | sort -u | tr '\n' ' ' | sed 's/ *$//'
)

[ -n "$MATCH" ] || { echo "gen-vboxvideo-personalities: no ids parsed from $SRC" >&2; exit 1; }
case " $MATCH " in
*" 0xbeef80ee "*) ;;
*) echo "gen-vboxvideo-personalities: VirtualBox id 0xbeef80ee missing — table parse broke" >&2; exit 1 ;;
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
echo "gen-vboxvideo-personalities: ${N} device id(s) from $(basename "$SRC"): ${MATCH}" >&2
