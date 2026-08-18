#!/bin/sh
# gen-virtio-gpu-personalities.sh — emit the IOKitPersonalities block for
# VirtIOGraphics.
#
#   usage: gen-virtio-gpu-personalities.sh <virtio_ids.h> <bundle-id> <IOClass>
#
# This one is NOT like gen-bochs/i915/amdgpu/radeon, and the difference is the
# whole reason it exists as its own script rather than a variant of them.
#
# Those drivers are PCI drivers: they carry a pci_device_id table, the kext's
# IOPCIPrimaryMatch is generated from it, and the same match both loads the
# kext AND binds the driver. virtio-gpu carries no such table. Its id_table is
#
#     { VIRTIO_ID_GPU, VIRTIO_DEV_ANY_ID }
#
# -- a VIRTIO device type, not a PCI id -- because it binds as a newbus child
# of virtio_pci via DRIVER_MODULE(), through the virtio bus, not by matching a
# PCI node directly.
#
# So the two halves are handled by two different mechanisms:
#
#   load  IOPCIPrimaryMatch on the PCI function, generated here. Its only job
#         is to get the kext into the kernel when the device is present.
#   bind  newbus, once the module is resident: virtio_pci enumerates its
#         children by device type and VIRTIO_SIMPLE_PROBE() claims type 16.
#
# The PCI id is DERIVED rather than read, because for a modern virtio device it
# is a function of the device type, fixed by the virtio 1.0 spec:
#
#     vendor 0x1af4, device 0x1040 + device_type
#     GPU is type 16, so 0x1040 + 16 = 0x1050
#
# Deriving it from VIRTIO_ID_GPU keeps the two in step: if the header is ever
# wrong or the driver retargeted, the id moves with it instead of silently
# disagreeing with what the driver actually probes for.
#
# Covers virtio-gpu-pci, virtio-gpu-gl-pci and virtio-vga alike -- all three are
# the same PCI function to the guest, differing only in whether the host also
# exposes VGA compatibility and a GL backend.
#
# There is deliberately NO legacy/transitional id. Transitional devices use
# 0x1000-0x103f and exist only for device types that predate virtio 1.0;
# virtio-gpu postdates it, so 0x1050 is the only id it is ever presented under.
set -eu

IDS=${1:?usage: gen-virtio-gpu-personalities.sh <virtio_ids.h> <bundle-id> <IOClass>}
BUNDLE_ID=${2:?missing bundle id}
IOCLASS=${3:?missing IOClass}

[ -f "$IDS" ] || { echo "gen-virtio-gpu-personalities: no such file: $IDS" >&2; exit 1; }

# VIRTIO_ID_GPU from the header the driver itself compiles against.
TYPE=$(awk '/^#define[ \t]+VIRTIO_ID_GPU[ \t]/ { print $3; exit }' "$IDS")
[ -n "$TYPE" ] || {
	echo "gen-virtio-gpu-personalities: VIRTIO_ID_GPU not found in $IDS" >&2
	exit 1
}
case "$TYPE" in
[0-9]*) ;;
*) echo "gen-virtio-gpu-personalities: VIRTIO_ID_GPU is not numeric: $TYPE" >&2; exit 1 ;;
esac

VENDOR=0x1af4
DEVICE=$((0x1040 + TYPE))

# IOKit match words are device<<16|vendor.
MATCH=$(printf '0x%04x%04x' "$DEVICE" "$((VENDOR))")

# The spec fixes this; if the arithmetic ever produces something else, the
# derivation is wrong and a silently non-matching kext is the worst outcome.
[ "$MATCH" = "0x10501af4" ] || {
	echo "gen-virtio-gpu-personalities: derived $MATCH, expected 0x10501af4 (type $TYPE)" >&2
	exit 1
}

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

echo "gen-virtio-gpu-personalities: virtio device type ${TYPE} -> PCI ${MATCH} (1af4:$(printf '%04x' "$DEVICE"))" >&2
