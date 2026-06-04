#!/bin/sh
# ko2kext.sh — wrap a FreeBSD .ko in a minimal Apple .kext bundle.
#
# NextBSD kext proof-of-concept (#183). Produces:
#
#   <Name>.kext/
#     Contents/
#       Info.plist            CFBundleExecutable/Identifier/PackageType=KEXT
#       MacOS/<Name>          the .ko, verbatim (kextload kldload's it)
#
# Minimal by design: no IOKitPersonalities, no OSBundleLibraries, no
# codesign. The faithful converter (MODULE_PNP_INFO -> IOKitPersonalities,
# kextlibs deps) is tracked in #182 / the .ko->.kext conversion plan (#179).
set -eu

usage() {
	echo "usage: ko2kext.sh <input.ko> <Name> <bundle-id> [outdir]" >&2
	echo "  e.g. ko2kext.sh if_dummy.ko Dummy org.nextbsd.kext.if_dummy" >&2
	exit 1
}

[ $# -ge 3 ] || usage
KO="$1"; NAME="$2"; BUNDLE_ID="$3"; OUTDIR="${4:-.}"
[ -f "$KO" ] || { echo "ko2kext: no such file: $KO" >&2; exit 1; }

KEXT="${OUTDIR}/${NAME}.kext"
rm -rf "$KEXT"
mkdir -p "${KEXT}/Contents/MacOS"

cp "$KO" "${KEXT}/Contents/MacOS/${NAME}"
chmod 0555 "${KEXT}/Contents/MacOS/${NAME}"

cat > "${KEXT}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleExecutable</key>
	<string>${NAME}</string>
	<key>CFBundleIdentifier</key>
	<string>${BUNDLE_ID}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>${NAME}</string>
	<key>CFBundlePackageType</key>
	<string>KEXT</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>OSBundleRequired</key>
	<string>Root</string>
</dict>
</plist>
EOF

echo "ko2kext: wrote ${KEXT}"
