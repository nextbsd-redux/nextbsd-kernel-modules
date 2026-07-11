#!/bin/sh
# gen-nvidia-personalities.sh — emit an Apple IOKitPersonalities block for an
# NVIDIA DRM/KMS graphics kext, derived from NVIDIA's own supported-gpus.json.
#
# Graphics kext series, NVIDIA track (NVIDIAGraphics<NNN>.kext <- nvidia-drm).
# Same idea as gen-i915/gen-amdgpu/gen-radeon: build the device-id match table
# from the driver's OWN data — never a hand-maintained list — so it can't drift
# from what the driver actually supports. Unlike the drm-kmod drivers (whose ids
# are pci_device_id rows in C source), NVIDIA ships the table as JSON in the
# driver package: supported-gpus/supported-gpus.json, shape:
#
#     { "chips": [
#         { "devid": "0x1E04", "name": "NVIDIA GeForce RTX 2080 Ti", "features": [...] },
#         { "devid": "0x13C2", "name": "...", "legacybranch": "470.xx", ... },
#         ...
#     ] }
#
# Every chip is vendor 0x10DE. IOPCIPrimaryMatch wants 0xDDDDVVVV (device high,
# vendor low), e.g. devid 0x1E04 -> "0x1e0410de".
#
# BRANCH PARTITION via the legacybranch field (this is the whole trick). NVIDIA
# tags any GPU dropped from the current unified driver with "legacybranch" (e.g.
# "580.xx", "470.xx", "390.xx", "340.xx", "304.xx"); GPUs still in the unified
# driver have NO legacybranch key. So ONE json + ONE script partitions every
# NextBSD NVIDIA kext with no arch table to maintain:
#
#   (default, no -b)   chips WITHOUT legacybranch  -> the current/main kext.
#                      Built against the 595 tarball this is exactly Turing+
#                      (595 drops Maxwell/Pascal/Volta to the 580 legacy branch),
#                      i.e. the Turing-DRM match set for NVIDIAGraphics595.kext —
#                      self-correcting on whatever driver version is fetched.
#   -b 580.xx          chips whose legacybranch == 580.xx  -> NVIDIAGraphics580.
#   -b 470.xx / 390.xx / 340.xx / 304.xx  -> the matching legacy kext.
#
# Because the partition keys on the json's own legacybranch string, the per-kext
# match sets are inherently disjoint (every chip has exactly one status). -x is
# kept as a belt-and-suspenders manual de-overlap, identical to gen-radeon, for
# the rare case two kexts are generated from different-version jsons.
#
# Output (stdout) is a complete `<key>IOKitPersonalities</key><dict>...</dict>`
# block ready to drop into Info.plist via `ko2kext.sh -p`.
#
# JSON is parsed with python3 (present in CI — the selftest/validate steps use
# plistlib). grep-scraping NVIDIA's json for a device match table is exactly the
# kind of brittle parse that silently drops a card, so the one hard step uses a
# real parser; the match-string build + output below are pure sh, identical in
# shape to the sibling generators for side-by-side review.
set -eu

usage() {
	echo "usage: gen-nvidia-personalities.sh [-b legacybranch] [-x exclude-file] <supported-gpus.json> <bundle-id> <ioclass>" >&2
	echo "  -b legacybranch  select chips whose legacybranch equals this string (e.g. 470.xx)." >&2
	echo "                   Omit for the current unified driver (chips with NO legacybranch)." >&2
	echo "  -x exclude-file  match words (0xDDDDVVVV, one per line) to omit — manual de-overlap," >&2
	echo "                   mirroring gen-radeon (normally unnecessary; the branch split is disjoint)." >&2
	echo "  <supported-gpus.json>  NVIDIA driver package's supported-gpus/supported-gpus.json" >&2
	exit 1
}

BRANCH=""          # empty = main/unified driver (no legacybranch)
EXCLUDE_FILE=""
while getopts "b:x:h" opt; do
	case "$opt" in
	b) BRANCH="$OPTARG" ;;
	x) EXCLUDE_FILE="$OPTARG" ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))

[ $# -eq 3 ] || usage
SRC="$1"; BUNDLE_ID="$2"; IOCLASS="$3"
[ -f "$SRC" ] || { echo "gen-nvidia-personalities: no such file: $SRC" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "gen-nvidia-personalities: python3 required to parse json" >&2; exit 1; }

VENDOR=10de   # PCI_VENDOR_ID_NVIDIA — every supported-gpus.json chip is NVIDIA.

# De-overlap set: match words to omit (lowercased, deduped). Empty when no -x.
EXCLUDES=""
if [ -n "$EXCLUDE_FILE" ]; then
	[ -f "$EXCLUDE_FILE" ] || { echo "gen-nvidia-personalities: exclude file not found: $EXCLUDE_FILE" >&2; exit 1; }
	EXCLUDES=$(grep -oE '0x[0-9A-Fa-f]{8}' "$EXCLUDE_FILE" | tr 'A-F' 'a-f' | sort -u)
fi

# Device ids for the requested branch = 4-hex devid of each matching chip.
# python3 does the json walk + legacybranch filter; emits one lowercase 4-hex
# id per line (no 0x), deduped. A chip missing/invalid devid (e.g. "*") is
# skipped. BRANCH="" selects chips with NO legacybranch; else exact-match.
IDS=$(BRANCH="$BRANCH" python3 - "$SRC" <<'PY'
import json, os, re, sys
src = sys.argv[1]
branch = os.environ.get("BRANCH", "")
with open(src) as f:
	data = json.load(f)
seen = []
for chip in data.get("chips", []):
	dev = chip.get("devid")
	if not dev:
		continue
	dev = str(dev).lower().replace("0x", "").strip()
	if not re.fullmatch(r"[0-9a-f]{4}", dev):
		continue
	legacy = chip.get("legacybranch")
	if branch == "":
		if legacy:            # want unified driver -> skip any legacy-tagged chip
			continue
	else:
		if legacy != branch:  # want a specific legacy branch -> exact match only
			continue
	seen.append(dev)
for d in sorted(set(seen)):
	print(d)
PY
)
if [ -z "$IDS" ]; then
	if [ -n "$BRANCH" ]; then
		echo "gen-nvidia-personalities: no chips with legacybranch '$BRANCH' in $(basename "$SRC")" >&2
	else
		echo "gen-nvidia-personalities: no non-legacy chips in $(basename "$SRC")" >&2
	fi
	exit 1
fi

# Build the space-separated IOPCIPrimaryMatch string: 0x<device><vendor>,
# skipping any word the -x exclude set already claims.
MATCH=""
N=0
NX=0
for id in $IDS; do
	dev=$id
	while [ ${#dev} -lt 4 ]; do dev="0${dev}"; done
	word="0x${dev}${VENDOR}"
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
[ -n "$MATCH" ] || { echo "gen-nvidia-personalities: every id was excluded by -x — nothing left" >&2; exit 1; }

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

if [ -n "$BRANCH" ]; then
	echo "gen-nvidia-personalities: ${N} NVIDIA device ids (legacybranch ${BRANCH}) from $(basename "$SRC") (excluded ${NX} via -x)" >&2
else
	echo "gen-nvidia-personalities: ${N} NVIDIA device ids (current unified driver) from $(basename "$SRC") (excluded ${NX} via -x)" >&2
fi
