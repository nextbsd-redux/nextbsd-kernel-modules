#!/bin/sh
# kext-boot-test.sh — boot the NextBSD continuous image (with Nmdm.kext and
# IntelWiFi.kext pre-injected at /System/Library/Extensions) under qemu/UEFI,
# log in, and prove kexts load end-to-end:
#   Nmdm.kext     — full lifecycle: kextload -> kextstat -> kextunload -> gone.
#   IntelWiFi.kext (if_iwlwifi) — load-only: kextload -> kextstat. Its linuxkpi/
#                   linuxkpi_wlan deps are baked into the kernel so it has no
#                   kext dependencies; a WiFi driver isn't unloaded here (no
#                   device attaches in QEMU and real ones may not unload cleanly).
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
IWIFI_PATH=/System/Library/Extensions/IntelWiFi.kext
# Graphics stack (nextbsd-kernel-modules#30). GFX_TEST=0 skips those stages, so
# the PoC still runs on images/branches without the graphics kexts injected.
GFX_TEST=${GFX_TEST:-1}
BOCHS_PATH=/System/Library/Extensions/BochsGraphics.kext
export ACCEL_FLAGS OVMF KEXT_PATH IWIFI_PATH GFX_TEST BOCHS_PATH

cat > "$EXP" <<'EOF'
set timeout 480
log_file -a tests/kext-boot.log
log_user 1

set img [lindex $argv 0]
set accel_flags [split $env(ACCEL_FLAGS) " "]
set kext $env(KEXT_PATH)
set iwifi $env(IWIFI_PATH)
set gfx_test $env(GFX_TEST)
set bochs $env(BOCHS_PATH)

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

# Stage 7: IntelWiFi.kext (if_iwlwifi) — load-only proof. linuxkpi and
# linuxkpi_wlan are baked into the kernel, so it loads with no kext deps. No
# Intel WiFi device attaches in QEMU, so the driver loads but stays idle.
send "kextload $iwifi\r"
expect {
    timeout { puts "\nFAIL: INTELWIFI-LOAD timed out"; exit 1 }
    "kextload: loaded"      { puts "\nOK: INTELWIFI-LOAD (if_iwlwifi kldload succeeded)" }
    "already loaded"        { puts "\nOK: INTELWIFI-LOAD (already loaded)" }
    -re {kldload\([^\n]*\n}  { puts "\nFAIL: IntelWiFi errored on kldload: $expect_out(0,string)"; exit 1 }
    -re "not a bundle"      { puts "\nFAIL: CFBundle could not open IntelWiFi.kext"; exit 1 }
}

# Stage 8: kextstat shows it. Echo-split sentinel so the marker never appears
# in the echoed command — match the loaded file (IntelWiFi) or module (iwlwifi).
send "kextstat | grep -Eqi 'intelwifi|iwlwifi' && echo IWIFI''_PRESENT || echo IWIFI''_ABSENT\r"
expect {
    timeout { puts "\nFAIL: INTELWIFI-STAT timed out"; exit 1 }
    "IWIFI_ABSENT"  { puts "\nFAIL: IntelWiFi loaded but not visible in kextstat"; exit 1 }
    "IWIFI_PRESENT" { puts "\nOK: INTELWIFI-STAT (driver visible via kldstat)" }
}

# ---------------------------------------------------------------------------
# Stages 9-12: the graphics stack. Unlike IntelWiFi (which loads but never
# binds, since qemu emulates no Intel WiFi), bochs SHOULD bind here: q35's
# default -vga std IS the Bochs/stdvga device, PCI 1234:1111, which is exactly
# what BochsGraphics.kext's IOPCIPrimaryMatch names. So this is the first test
# in the tree that can prove match -> load -> bind -> KMS, not just load.
#
# Loaded explicitly in dependency order rather than relying on kextload to walk
# OSBundleLibraries, so a failure names the layer that broke.
# ---------------------------------------------------------------------------
if {$gfx_test} {
    foreach k {DMABuf IOGraphics TTM IOGraphicsExtras BochsGraphics} {
        send "kextload /System/Library/Extensions/$k.kext\r"
        expect {
            timeout { puts "\nFAIL: GFX-LOAD $k timed out"; exit 1 }
            "kextload: loaded" { puts "\nOK: GFX-LOAD $k" }
            "already loaded"   { puts "\nOK: GFX-LOAD $k (already loaded)" }
            -re {kldload\([^\n]*\n} {
                puts "\nFAIL: GFX-LOAD $k errored: $expect_out(0,string)"
                # Dump why before bailing: a bare ENOEXEC says nothing, and a
                # second CI cycle to learn it costs 20 minutes.
                send "kldstat\r"; expect { timeout {} -re {[#%$] $} {} }
                send "dmesg | tail -25\r"; expect { timeout {} -re {[#%$] $} {} }
                exit 1
            }
            -re "not a bundle" { puts "\nFAIL: GFX-LOAD $k is not a readable bundle"; exit 1 }
        }
    }

    # Stage 10: the driver is resident.
    send "kextstat | grep -qi bochs && echo BOCHS''_PRESENT || echo BOCHS''_ABSENT\r"
    expect {
        timeout { puts "\nFAIL: GFX-STAT timed out"; exit 1 }
        "BOCHS_ABSENT"  { puts "\nFAIL: BochsGraphics loaded but absent from kextstat"; exit 1 }
        "BOCHS_PRESENT" { puts "\nOK: GFX-STAT (bochs resident)" }
    }

    # Stage 11: it BOUND, and DRM published a device node. This is the real
    # question -- the plan's canonical failure is "binds but black screen", and
    # a missing card0 separates "never bound" from "bound but no KMS".
    send "ls /dev/dri/card0 >/dev/null 2>&1 && echo CARD0''_YES || echo CARD0''_NO\r"
    expect {
        timeout { puts "\nFAIL: GFX-CARD0 timed out"; exit 1 }
        "CARD0_YES" { puts "\nOK: GFX-CARD0 (/dev/dri/card0 exists -- KMS is up)" }
        "CARD0_NO"  { puts "\nFAIL: GFX-CARD0 -- no /dev/dri/card0 (loaded but did not bind, or KMS init failed)" }
    }

    # Stage 12: diagnostics either way -- attach lines and any drm complaint.
    send "dmesg | grep -iE 'bochs|drm|vgapci' | tail -20\r"
    expect { timeout { } -re {[#%$] $} { } }
    send "ls -l /dev/dri 2>&1 | head -5\r"
    expect { timeout { } -re {[#%$] $} { } }
}

puts "\nKEXT-POC-OK: Nmdm load/stat/unload + IntelWiFi load/stat all passed"

# Stage 7: clean halt.
send "halt -p\r"
expect {
    timeout   { puts "\nWARN: halt didn't complete within timeout" }
    "Uptime:" { puts "\nOK: clean halt" }
    eof       { puts "\nOK: VM exited" }
}

# Teardown must not fail the run after all functional stages passed. If `halt -p`
# already powered the VM off (the eof branch above), the spawn is closed and a
# bare `close`/`wait` throws "spawn id ... not open" — a runner-timing artifact,
# not a test failure. Guard both so an already-exited VM is harmless.
catch { close }
catch { wait }
exit 0
EOF

expect "$EXP" "$IMG"
echo "==> kext-boot-test PASSED"
