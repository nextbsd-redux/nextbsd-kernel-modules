#!/bin/sh
# ko2kext.sh — wrap a FreeBSD .ko in an Apple .kext bundle.
#
# NextBSD kext converter (#183 PoC, extended for #179/#182). Produces:
#
#   <Name>.kext/
#     Contents/
#       Info.plist            CFBundleExecutable/Identifier/PackageType=KEXT,
#                             optional OSBundleLibraries (dependency graph)
#       MacOS/<Name>          the .ko, verbatim (kextload kldload's it)
#       Resources/firmware/   optional bundled firmware blobs (-f)
#
# OSBundleLibraries (-l) is what the OSKext engine / kextdeps resolve to load
# dependencies in order (e.g. IntelWiFi -> LinuxKPIWlan). Firmware (-f) is
# bundled Apple-style under Contents/Resources/firmware. Still no codesign
# (pre-SIP). The faithful MODULE_PNP_INFO -> IOKitPersonalities step is #179.
set -eu

usage() {
	cat >&2 <<'U'
usage: ko2kext.sh [-o outdir] [-v version] [-l dep-bundle-id]... [-f firmware]... <input.ko> <Name> <bundle-id>

  -o outdir        where to write <Name>.kext       (default: .)
  -v version       CFBundleVersion                   (default: 1.0)
  -l dep-bundle-id add an OSBundleLibraries dependency (repeatable)
                     e.g. -l org.nextbsd.kpi.LinuxKPIWlan
  -f firmware      bundle firmware into Contents/Resources/firmware (repeatable).
                     A file is copied in; a directory has its contents copied in
                     flat (symlinks dereferenced) so e.g. all iwlwifi *.ucode
                     land directly under firmware/ where firmware(9) finds them.

  e.g. ko2kext.sh -l org.nextbsd.kpi.LinuxKPIWlan \
                  if_iwlwifi.ko IntelWiFi org.nextbsd.driver.IntelWiFi
U
	exit 1
}

OUTDIR="."
VERSION="1.0"
DEPS=""        # newline-separated bundle ids
FWS=""         # newline-separated firmware paths
while getopts "o:v:l:f:h" opt; do
	case "$opt" in
	o) OUTDIR="$OPTARG" ;;
	v) VERSION="$OPTARG" ;;
	l) DEPS="${DEPS}${OPTARG}
" ;;
	f) FWS="${FWS}${OPTARG}
" ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))

[ $# -eq 3 ] || usage
KO="$1"; NAME="$2"; BUNDLE_ID="$3"
[ -f "$KO" ] || { echo "ko2kext: no such file: $KO" >&2; exit 1; }

KEXT="${OUTDIR}/${NAME}.kext"
rm -rf "$KEXT"
mkdir -p "${KEXT}/Contents/MacOS"

cp "$KO" "${KEXT}/Contents/MacOS/${NAME}"
chmod 0555 "${KEXT}/Contents/MacOS/${NAME}"

# Bundle firmware, if any, under Contents/Resources/firmware (Apple-style).
if [ -n "$FWS" ]; then
	mkdir -p "${KEXT}/Contents/Resources/firmware"
	printf '%s' "$FWS" | while IFS= read -r fw; do
		[ -n "$fw" ] || continue
		[ -e "$fw" ] || { echo "ko2kext: firmware not found: $fw" >&2; exit 1; }
		if [ -d "$fw" ]; then
			# Directory: copy its contents flat into firmware/, dereferencing
			# symlinks so versioned firmware (linux-firmware uses symlinks) is
			# stored as real files the kernel can open by the requested name.
			cp -RL "$fw"/. "${KEXT}/Contents/Resources/firmware/"
		else
			cp -L "$fw" "${KEXT}/Contents/Resources/firmware/"
		fi
	done
fi

# Build the optional OSBundleLibraries dict from -l deps.
OSBL=""
if [ -n "$DEPS" ]; then
	OSBL="	<key>OSBundleLibraries</key>
	<dict>
"
	# Dependency value is the minimum compatible version; 1.0 for our kexts.
	while IFS= read -r dep; do
		[ -n "$dep" ] || continue
		OSBL="${OSBL}		<key>${dep}</key>
		<string>1.0</string>
"
	done <<EOF
$DEPS
EOF
	OSBL="${OSBL}	</dict>
"
fi

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
	<string>${VERSION}</string>
	<key>CFBundleVersion</key>
	<string>${VERSION}</string>
${OSBL}	<key>OSBundleRequired</key>
	<string>Root</string>
</dict>
</plist>
EOF

echo "ko2kext: wrote ${KEXT}"
[ -n "$DEPS" ] && echo "ko2kext:   OSBundleLibraries: $(printf '%s' "$DEPS" | tr '\n' ' ')"
[ -n "$FWS" ]  && echo "ko2kext:   firmware: $(ls "${KEXT}/Contents/Resources/firmware" 2>/dev/null | tr '\n' ' ')"
exit 0
