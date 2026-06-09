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
         "$TOOLS"/gen-radeon-personalities.sh

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

# --- amdgpu: {0x1002, 0x....} pci_device_id rows ---
cat > "$WORK/amdgpu_drv.c" <<'EOF'
static const struct pci_device_id pciidlist[] = {
	{0x1002, 0x67DF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0, 0, 0}
};
EOF
AMD=$("$TOOLS/gen-amdgpu-personalities.sh" "$WORK/amdgpu_drv.c" org.nextbsd.kext.amdgraphics AMDGraphics)
printf '%s\n' "$AMD"
printf '%s\n' "$AMD" | grep -q '0x67df1002' || fail "amdgpu: 0x67DF missing"

# --- radeon: radeon_PCI_IDS block PLUS an r128_PCI_IDS block (also vendor
#     0x1002) that MUST be excluded by the macro scoping ---
cat > "$WORK/drm_pciids.h" <<'EOF'
#define radeon_PCI_IDS \
	{0x1002, 0x3150, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RV380|RADEON_IS_MOBILITY}, \
	{0x1002, 0x9999, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE}
#define r128_PCI_IDS \
	{0x1002, 0x5046, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, \
	{0, 0, 0}
EOF
RAD=$("$TOOLS/gen-radeon-personalities.sh" "$WORK/drm_pciids.h" org.nextbsd.kext.radeongraphics RadeonGraphics)
printf '%s\n' "$RAD"
printf '%s\n' "$RAD" | grep -q '0x31501002' || fail "radeon: 0x3150 missing"
printf '%s\n' "$RAD" | grep -q '0x99991002' || fail "radeon: 0x9999 missing"
if printf '%s\n' "$RAD" | grep -q '0x50461002'; then
	fail "radeon: r128 id 0x5046 wrongly included (macro-scope leak)"
fi

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

echo "ALL GRAPHICS PERSONALITY SELF-TESTS PASSED"
