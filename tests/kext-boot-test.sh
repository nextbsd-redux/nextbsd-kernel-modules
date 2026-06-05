#!/bin/sh
# kext-boot-test.sh — boot the NextBSD continuous image (with Nmdm.kext
# pre-injected at /System/Library/Extensions) under qemu/UEFI, log in, and
# prove the kext loads end-to-end: kextload -> kextstat shows it ->
# kextunload -> it's gone.
#
# NextBSD kext proof-of-concept (#183). Loader/login/halt stages are modeled
# on nextbsd tests/boot-test.sh; only the test stage differs (kext assertions
# instead of the mach marker suite). Takes a raw .img (or .img.zip/.gz).
set -eu

IMG="${1:?usage: kext-boot-test.sh <disk.img|.img.zip|.img.gz>}"
EXP=tests/kext-boot-test.exp
mkdir -p tests

case "$IMG" in
*.zip)
    RAW=tests/disk.img
    echo "==> extracting $IMG -> $RAW"
    MEMBER=$(unzip -Z1 "$IMG" | grep -E '\.img$' | head -1)
    [ -n "$MEMBER" ] || { echo "FAIL: no .img member in $IMG" >&2; exit 1; }
    unzip -p "$IMG" "$MEMBER" > "$RAW"
    IMG=$RAW
    ;;
*.gz)
    RAW=tests/disk.img
    echo "==> decompressing $IMG -> $RAW"
    gunzip -c "$IMG" > "$RAW"
    IMG=$RAW
    ;;
esac

echo "==> kext boot test: $IMG"
ls -lh "$IMG"

# Acceleration: KVM if available, else TCG.
if [ -e /dev/kvm ]; then sudo chmod 666 /dev/kvm 2>/dev/null || true; fi
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    ACCEL_FLAGS="-accel kvm -cpu host"
    echo "==> using KVM acceleration"
else
    ACCEL_FLAGS="-accel tcg,thread=single -cpu qemu64"
    echo "==> using TCG (single-thread)"
fi

OVMF=""
for f in /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd /usr/share/qemu/OVMF.fd; do
    [ -f "$f" ] && { OVMF="$f"; break; }
done
[ -n "$OVMF" ] || { echo "ERROR: no OVMF firmware found"; exit 1; }
echo "==> using UEFI firmware: $OVMF"

KEXT_PATH=/System/Library/Extensions/Nmdm.kext
export ACCEL_FLAGS OVMF KEXT_PATH

cat > "$EXP" <<'EOF'
set timeout 480
log_file -a tests/kext-boot.log
log_user 1

set img [lindex $argv 0]
set accel_flags [split $env(ACCEL_FLAGS) " "]
set kext $env(KEXT_PATH)

eval spawn qemu-system-x86_64 \
    -m 4G \
    -machine q35 \
    -bios $env(OVMF) \
    $accel_flags \
    -drive file=$img,format=raw,if=virtio \
    -nic user,model=e1000 \
    -display none -serial stdio \
    -no-reboot

# Stage 0: loader OK prompt -> enable serial console -> boot.
expect {
    timeout { puts "\nFAIL: didn't see loader autoboot prompt within 60s"; exit 1 }
    -re "Hit \\\[Enter\\\]" { send " " }
    "Booting"           { send " " }
    "FreeBSD/amd64 EFI" { send " " }
}
expect {
    timeout { puts "\nFAIL: didn't reach loader OK prompt within 30s"; exit 1 }
    "OK " {}
}
send "set console=comconsole\r";        expect "set console=comconsole";        expect "OK "
send "set boot_serial=YES\r";           expect "set boot_serial=YES";           expect "OK "
send "set comconsole_speed=115200\r";   expect "set comconsole_speed=115200";   expect "OK "
send "set boot_multicons=YES\r";        expect "set boot_multicons=YES";        expect "OK "
send "boot\r"

# Stage 1: reach the login prompt (boot completes: mach.ko -> root -> launchd -> getty).
expect {
    timeout { puts "\nFAIL: 'login:' prompt not seen within 8 minutes"; exit 1 }
    "login:" { puts "\nOK: boot reached the login prompt" }
}

# Stage 2: log in as root (no password on the live image).
send "root\r"
expect {
    timeout { puts "\nFAIL: no response after sending root"; exit 1 }
    "Password:"       { send "\r"; exp_continue }
    "Login incorrect" { puts "\nFAIL: root login rejected"; exit 1 }
    -re {[#%$] $}     { puts "\nOK: at root shell prompt" }
}

# Stage 3: kextload the bundle. Its ": loaded"/"already loaded" output cannot
# appear in the echoed command line, so matching it is race-free.
send "kextload $kext\r"
expect {
    timeout { puts "\nFAIL: KEXT-LOAD timed out"; exit 1 }
    "kextload: loaded"  { puts "\nOK: KEXT-LOAD (kldload of the bundled .ko succeeded)" }
    "already loaded"    { puts "\nOK: KEXT-LOAD (already loaded)" }
    -re {kldload\([^\n]*\n} { puts "\nFAIL: kextload errored on kldload: $expect_out(0,string)"; exit 1 }
    -re "not a bundle"  { puts "\nFAIL: CFBundle could not open the .kext"; exit 1 }
}

# Stage 4: kextstat shows it. Echo-split sentinel (STAT''_*) so the marker
# never appears in the echoed command — only in the command's output.
send "kextstat | grep -q Nmdm && echo STAT''_PRESENT || echo STAT''_ABSENT\r"
expect {
    timeout { puts "\nFAIL: KEXT-STAT timed out"; exit 1 }
    "STAT_ABSENT"  { puts "\nFAIL: loaded kext not visible in kextstat"; exit 1 }
    "STAT_PRESENT" { puts "\nOK: KEXT-STAT (module visible via kldstat enumeration)" }
}

# Stage 5: kextunload.
send "kextunload $kext\r"
expect {
    timeout { puts "\nFAIL: KEXT-UNLOAD timed out"; exit 1 }
    "kextunload: unloaded" { puts "\nOK: KEXT-UNLOAD (kldunload succeeded)" }
    "not loaded"           { puts "\nFAIL: kextunload couldn't find the module"; exit 1 }
}

# Stage 6: confirm it's gone.
send "kextstat | grep -q Nmdm && echo GONE''_NO || echo GONE''_YES\r"
expect {
    timeout { puts "\nFAIL: post-unload check timed out"; exit 1 }
    "GONE_NO"  { puts "\nFAIL: module still loaded after kextunload"; exit 1 }
    "GONE_YES" { puts "\nOK: KEXT-GONE (unloaded cleanly)" }
}

puts "\nKEXT-POC-OK: convert -> kextload -> kextstat -> kextunload all passed"

# Stage 7: clean halt.
send "halt -p\r"
expect {
    timeout   { puts "\nWARN: halt didn't complete within timeout" }
    "Uptime:" { puts "\nOK: clean halt" }
    eof       { puts "\nOK: VM exited" }
}

close
wait
exit 0
EOF

expect "$EXP" "$IMG"
echo "==> kext-boot-test PASSED"
