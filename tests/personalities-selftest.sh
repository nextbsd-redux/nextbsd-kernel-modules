#!/bin/sh
# personalities-selftest.sh — prove the graphics-kext IOKitPersonalities
# generators (i915 / amdgpu / radeon) on every PR, without needing the full
# drm-kmod build.
#
# The drm-kmod source build isn't wired into CI yet, so we exercise the parsers
# against synthetic PCI-id fixtures shaped exactly like the real i915_pciids.h /
# amdgpu_drv.c / drm_pciids.h, and assert the emitted IOPCIPrimaryMatch table.
# Critically asserts radeon EXCLUDES r128 (also vendor 0x1002) — the macro-scope
# trap a naive whole-file 0x1002 grep would fall into.
#
# Mirrors gen-em / gen-iwlwifi being proven by the module + kext-smoke jobs; this
# is the equivalent proof for the graphics generators. Runs on PRs (build.yml);
# publish stays gated to main.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOLS="$HERE/../tools"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $1" >&2; exit 1; }

chmod +x "$TOOLS"/gen-i915-personalities.sh \
         "$TOOLS"/gen-amdgpu-personalities.sh \
         "$TOOLS"/gen-radeon-personalities.sh \
         "$TOOLS"/gen-nvidia-personalities.sh

# --- i915: INTEL_VGA_DEVICE macro rows; 0x591E is uppercase to prove
#     case-normalisation ---
cat > "$WORK/i915_pciids.h" <<'EOF'
#define INTEL_KBL_GT2_IDS(info) \
	INTEL_VGA_DEVICE(0x5916, info), \
	INTEL_VGA_DEVICE(0x591E, info)
EOF
I915=$("$TOOLS/gen-i915-personalities.sh" "$WORK/i915_pciids.h" org.nextbsd.kext.intelgraphics IntelGraphics)
printf '%s\n' "$I915"
printf '%s\n' "$I915" | grep -q '0x59168086' || fail "i915: 0x5916 missing"
printf '%s\n' "$I915" | grep -q '0x591e8086' || fail "i915: 0x591E not lowercased"

# --- amdgpu: {0x1002, 0x....} pci_device_id rows. 0x6649 is a CIK id (Bonaire)
#     that ALSO appears in the radeon fixture below — the de-overlap case. ---
cat > "$WORK/amdgpu_drv.c" <<'EOF'
static const struct pci_device_id pciidlist[] = {
	{0x1002, 0x67DF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x6649, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0, 0, 0}
};
EOF
AMD=$("$TOOLS/gen-amdgpu-personalities.sh" "$WORK/amdgpu_drv.c" org.nextbsd.kext.amdgraphics AMDGraphics)
printf '%s\n' "$AMD"
printf '%s\n' "$AMD" | grep -q '0x67df1002' || fail "amdgpu: 0x67DF missing"
printf '%s\n' "$AMD" | grep -q '0x66491002' || fail "amdgpu: 0x6649 (CIK Bonaire) missing"

# amdgpu's match words become radeon's de-overlap exclusion set — the way the
# build wires it: generate AMDGraphics first, feed its ids to gen-radeon -x.
printf '%s\n' "$AMD" | grep -oE '0x[0-9a-f]{8}' | sort -u > "$WORK/amdgpu-match.txt"

# --- radeon: radeon_PCI_IDS (incl. 0x6649, the CIK overlap with amdgpu) PLUS an
#     r128_PCI_IDS block (also vendor 0x1002) that MUST be excluded by macro
#     scoping. Generated WITH -x so 0x6649 is de-overlapped to amdgpu. ---
cat > "$WORK/drm_pciids.h" <<'EOF'
#define radeon_PCI_IDS \
	{0x1002, 0x3150, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RV380|RADEON_IS_MOBILITY}, \
	{0x1002, 0x6649, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|RADEON_IS_MOBILITY}
#define r128_PCI_IDS \
	{0x1002, 0x5046, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, \
	{0, 0, 0}
EOF
RAD=$("$TOOLS/gen-radeon-personalities.sh" -x "$WORK/amdgpu-match.txt" \
	"$WORK/drm_pciids.h" org.nextbsd.kext.radeongraphics RadeonGraphics)
printf '%s\n' "$RAD"
printf '%s\n' "$RAD" | grep -q '0x31501002' || fail "radeon: 0x3150 (radeon-only) missing"
if printf '%s\n' "$RAD" | grep -q '0x66491002'; then
	fail "radeon: 0x6649 (CIK) NOT de-overlapped — amdgpu should own it"
fi
if printf '%s\n' "$RAD" | grep -q '0x50461002'; then
	fail "radeon: r128 id 0x5046 wrongly included (macro-scope leak)"
fi

# --- cross-check: AMDGraphics and RadeonGraphics match sets must be DISJOINT ---
printf '%s\n' "$RAD" | grep -oE '0x[0-9a-f]{8}' | sort -u > "$WORK/radeon-match.txt"
OVERLAP=$(comm -12 "$WORK/amdgpu-match.txt" "$WORK/radeon-match.txt")
[ -z "$OVERLAP" ] || fail "AMDGraphics and RadeonGraphics share ids: $OVERLAP"
echo "de-overlap OK: AMD/Radeon match sets disjoint"

# --- well-formedness: each fragment must parse as a plist when wrapped, and the
#     match table must be all 10-char 0xDDDDVVVV words ---
if command -v python3 >/dev/null 2>&1; then
	for pair in "IntelGraphics:$I915" "AMDGraphics:$AMD" "RadeonGraphics:$RAD"; do
		cls=${pair%%:*}
		frag=${pair#*:}
		{
			printf '%s' '<?xml version="1.0" encoding="UTF-8"?>'
			printf '%s' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
			printf '%s' '<plist version="1.0"><dict>'
			printf '%s' "$frag"
			printf '%s' '</dict></plist>'
		} > "$WORK/wrap.plist"
		python3 - "$WORK/wrap.plist" "$cls" <<'PY'
import plistlib, sys
d = plistlib.load(open(sys.argv[1], 'rb'))
cls = sys.argv[2]
p = d['IOKitPersonalities'][cls]
assert p['IOProviderClass'] == 'IOPCIDevice', p
assert p['IOClass'] == cls, p
ids = p['IOPCIPrimaryMatch'].split()
assert ids and all(x.startswith('0x') and len(x) == 10 for x in ids), ids[:3]
print(f"{cls}: plist OK, {len(ids)} device ids")
PY
	done
fi

# --- nvidia: supported-gpus.json with a Turing chip (no legacybranch, so the
#     current unified driver owns it) plus one 580.xx and one 470.xx legacy chip.
#     0x1e07 is uppercase in the fixture to prove case-normalisation. Asserts the
#     branch partition: default mode = Turing only; -b picks exactly one branch;
#     the three match sets are disjoint (the legacybranch split guarantees it). ---
cat > "$WORK/supported-gpus.json" <<'EOF'
{ "chips": [
	{ "devid": "0x1E07", "name": "NVIDIA GeForce RTX 2080 Ti (TU102)", "features": ["drmkms"] },
	{ "devid": "0x2182", "name": "NVIDIA GeForce GTX 1660 Ti (TU116)", "features": ["drmkms"] },
	{ "devid": "0x13C2", "name": "NVIDIA GeForce GTX 970 (Maxwell)", "legacybranch": "580.xx" },
	{ "devid": "0x0FC6", "name": "NVIDIA GeForce GTX 650 (Kepler)", "legacybranch": "470.xx" },
	{ "devid": "*",      "name": "wildcard row that must be skipped" }
] }
EOF
NV=$("$TOOLS/gen-nvidia-personalities.sh" "$WORK/supported-gpus.json" org.nextbsd.driver.NVIDIAGraphics595 NVIDIAGraphics595)
printf '%s\n' "$NV"
printf '%s\n' "$NV" | grep -q '0x1e0710de' || fail "nvidia: 0x1E07 (Turing) missing or not lowercased"
printf '%s\n' "$NV" | grep -q '0x218210de' || fail "nvidia: 0x2182 (Turing) missing"
if printf '%s\n' "$NV" | grep -q '0x13c210de'; then
	fail "nvidia: 0x13C2 (580 legacy) leaked into the unified-driver kext"
fi
if printf '%s\n' "$NV" | grep -q '0x0fc610de'; then
	fail "nvidia: 0x0FC6 (470 legacy) leaked into the unified-driver kext"
fi
if printf '%s\n' "$NV" | grep -q '0x2a2a10de'; then
	fail "nvidia: wildcard '*' devid was not skipped"
fi

# -b 470.xx must select ONLY the Kepler chip.
NV470=$("$TOOLS/gen-nvidia-personalities.sh" -b 470.xx "$WORK/supported-gpus.json" org.nextbsd.driver.NVIDIAGraphics470 NVIDIAGraphics470)
printf '%s\n' "$NV470" | grep -q '0x0fc610de' || fail "nvidia -b 470.xx: 0x0FC6 missing"
if printf '%s\n' "$NV470" | grep -q '0x1e0710de'; then
	fail "nvidia -b 470.xx: Turing id 0x1E07 wrongly included"
fi

# cross-check: the unified (595) and 470 match sets must be DISJOINT.
printf '%s\n' "$NV"    | grep -oE '0x[0-9a-f]{8}' | sort -u > "$WORK/nv595-match.txt"
printf '%s\n' "$NV470" | grep -oE '0x[0-9a-f]{8}' | sort -u > "$WORK/nv470-match.txt"
NVOVERLAP=$(comm -12 "$WORK/nv595-match.txt" "$WORK/nv470-match.txt")
[ -z "$NVOVERLAP" ] || fail "NVIDIAGraphics595 and NVIDIAGraphics470 share ids: $NVOVERLAP"
echo "de-overlap OK: NVIDIA 595/470 match sets disjoint (legacybranch partition)"

# well-formedness of the nvidia fragment (same plist check as above).
if command -v python3 >/dev/null 2>&1; then
	{
		printf '%s' '<?xml version="1.0" encoding="UTF-8"?>'
		printf '%s' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
		printf '%s' '<plist version="1.0"><dict>'
		printf '%s' "$NV"
		printf '%s' '</dict></plist>'
	} > "$WORK/wrap.plist"
	python3 - "$WORK/wrap.plist" NVIDIAGraphics595 <<'PY'
import plistlib, sys
d = plistlib.load(open(sys.argv[1], 'rb'))
cls = sys.argv[2]
p = d['IOKitPersonalities'][cls]
assert p['IOProviderClass'] == 'IOPCIDevice', p
assert p['IOClass'] == cls, p
ids = p['IOPCIPrimaryMatch'].split()
assert ids and all(x.startswith('0x') and len(x) == 10 for x in ids), ids[:3]
print(f"{cls}: plist OK, {len(ids)} device ids")
PY
fi

echo "ALL GRAPHICS PERSONALITY SELF-TESTS PASSED"
