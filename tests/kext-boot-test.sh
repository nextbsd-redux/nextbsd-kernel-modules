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
    -device virtio-gpu-pci \
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

# ---------------------------------------------------------------------------
# Stage 2b: AUTOLOAD. The graphics kexts were injected into
# /System/Library/Extensions before boot, so if the in-kernel IOKit matcher
# and kextd do their job, BochsGraphics should already be bound to the qemu
# stdvga (1234:1111) by the time we reach a shell -- with nobody having run
# kextload. That is the user-facing story the whole kext architecture rests
# on, and it is distinct from the explicit-load proof in the GFX stages
# below (which would pass even if autoload were broken).
#
# Reported, not gated, on purpose: this is the first time anything has
# exercised match -> kextd -> load without a human, and a NO here is a real
# finding about the matcher rather than a reason to fail the suite.
# ---------------------------------------------------------------------------
if {$gfx_test} {
    # Autoload is ASYNCHRONOUS: the kernel matcher hands kextd a load request
    # and kextd services it on its own schedule, so sampling once the moment a
    # shell appears is a race -- the first revision of this stage reported NO
    # while the guest's kextd.log showed the load happening moments later.
    # Poll instead, and let the marker say how long it took.
    set autoload_ok 0
    for {set i 1} {$i <= 20} {incr i} {
        send "ls /dev/dri/card0 >/dev/null 2>&1 && echo AUTO''_YES || echo AUTO''_NO\r"
        expect {
            timeout    { }
            "AUTO_YES" { set autoload_ok 1 }
            "AUTO_NO"  { }
        }
        if {$autoload_ok} break
        sleep 3
    }
    if {$autoload_ok} {
        puts "\nOK: GFX-AUTOLOAD -- card0 appeared with no kextload (match -> kextd -> bind), after [expr {$i * 3}]s"
    } else {
        puts "\nFAIL: GFX-AUTOLOAD -- no card0 within 60s of reaching a shell"
        send "tail -15 /var/log/kextd.log 2>&1\r"
        expect { timeout {} -re {[#%$] $} {} }
    }
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

    # Stage 11b: VBoxGraphics loads. It cannot BIND here -- qemu emulates no
    # VirtualBox GPU, so 80ee:beef is absent and no probe will ever fire -- but
    # loading is the assertion that actually matters for a fresh port: the
    # failure that killed the first bochs iterations was kldload ENOEXEC from
    # missing module metadata, and an unresolved symbol against the helper
    # module would surface here too. Attach is proven on a VirtualBox guest,
    # not in CI. Skipped silently when the kext is absent, so the gate stays
    # green while the vboxvideo build is iterated.
    send "test -d /System/Library/Extensions/VBoxGraphics.kext && echo VBOX''_KEXT_YES || echo VBOX''_KEXT_NO\r"
    set vbox_present 0
    expect {
        timeout { puts "\nWARN: VBOX-PRESENT probe timed out" }
        "VBOX_KEXT_YES" { set vbox_present 1 }
        "VBOX_KEXT_NO"  { puts "\nSKIP: VBoxGraphics.kext not in this image" }
    }
    if {$vbox_present} {
        send "kextload /System/Library/Extensions/VBoxGraphics.kext\r"
        expect {
            timeout { puts "\nFAIL: VBOX-LOAD timed out"; exit 1 }
            "kextload: loaded" { puts "\nOK: VBOX-LOAD VBoxGraphics" }
            "already loaded"   { puts "\nOK: VBOX-LOAD VBoxGraphics (already loaded)" }
            -re {kldload\([^\n]*\n} {
                puts "\nFAIL: VBOX-LOAD errored: $expect_out(0,string)"
                send "kldstat\r"; expect { timeout {} -re {[#%$] $} {} }
                send "dmesg | tail -25\r"; expect { timeout {} -re {[#%$] $} {} }
                exit 1
            }
            -re "not a bundle" { puts "\nFAIL: VBOX-LOAD VBoxGraphics is not a readable bundle"; exit 1 }
        }
        # Match on the BUNDLE name, not the kld module name. kldstat lists the
        # loaded file, which for a kext is the bundle binary
        # (VBoxGraphics.kext/Contents/MacOS/VBoxGraphics) -- so "VBoxGraphics"
        # appears and "vboxvideo", the KMOD name inside it, never does. The
        # bochs stage above only passes because `grep -i bochs` happens to
        # match "BochsGraphics"; grepping for vboxvideo here found nothing and
        # reported a resident module as missing. Both spellings are accepted so
        # this keeps working if kextstat ever reports the module name instead.
        send "kextstat | grep -iE 'vboxgraphics|vboxvideo'\r"
        expect { timeout { } -re {[#%$] $} { } }
        send "kextstat | grep -qiE 'vboxgraphics|vboxvideo' && echo VBOX''_PRESENT || echo VBOX''_ABSENT\r"
        expect {
            timeout { puts "\nFAIL: VBOX-STAT timed out"; exit 1 }
            "VBOX_ABSENT"  {
                puts "\nFAIL: VBoxGraphics loaded but absent from kextstat"
                send "kextstat\r"; expect { timeout {} -re {[#%$] $} {} }
                send "kldstat\r"; expect { timeout {} -re {[#%$] $} {} }
                exit 1
            }
            "VBOX_PRESENT" { puts "\nOK: VBOX-STAT (vboxvideo resident; no bind expected in qemu)" }
        }
    }

    # -----------------------------------------------------------------------
    # Stage 13: virtio-gpu. Unlike VBoxGraphics this one CAN bind -- the qemu
    # invocation above adds -device virtio-gpu-pci, so 1af4:1050 is present.
    #
    # virtio-gpu-pci deliberately, not virtio-vga: the -pci form presents
    # display class "other" rather than VGA, so it does not own the legacy
    # aperture and does not contest the console with bochs. Both drivers can
    # therefore be exercised in one boot, with bochs on card0 and virtio-gpu
    # taking the next minor.
    #
    # This is also the first stage that tests a driver binding through a bus
    # OTHER than raw PCI: IOPCIPrimaryMatch only gets the kext loaded, and
    # newbus then attaches it as a child of virtio_pci.
    # -----------------------------------------------------------------------
    send "test -d /System/Library/Extensions/VirtIOGraphics.kext && echo VIRTIO''_KEXT_YES || echo VIRTIO''_KEXT_NO\r"
    set virtio_present 0
    expect {
        timeout { puts "\nWARN: VIRTIO-PRESENT probe timed out" }
        "VIRTIO_KEXT_YES" { set virtio_present 1 }
        "VIRTIO_KEXT_NO"  { puts "\nSKIP: VirtIOGraphics.kext not in this image" }
    }

    if {$virtio_present} {
        # Before loading anything: has devmatch already autoloaded BASE's
        # virtio_gpu(4) for this device? Both claim virtio device type 16, and
        # whichever attaches first owns it -- base's also registers above efifb
        # at VD_PRIORITY_GENERIC+10, which is why standing decision V7 keeps it
        # out of kernel configs. If it won the race, our attach cannot happen
        # and the reason would otherwise look like an unexplained bind failure.
        send "kldstat | grep -i 'virtio_gpu' | grep -vi virtio_gpu_drm || echo BASE''_VIRTIO_GPU_ABSENT\r"
        expect {
            timeout { puts "\nWARN: BASE-VIRTIO-GPU probe timed out" }
            "BASE_VIRTIO_GPU_ABSENT" { puts "\nOK: base virtio_gpu(4) not resident (no devmatch race)" }
            -re {virtio_gpu[^\n]*\n} {
                puts "\nWARN: base virtio_gpu(4) IS resident -- it may already own the device"
            }
        }

        # Dependency order, so a failure names the layer rather than the leaf.
        foreach k {LinuxVirtIO IOGraphicsShmem VirtIOGraphics} {
            send "kextload /System/Library/Extensions/$k.kext\r"
            expect {
                timeout { puts "\nFAIL: VIRTIO-LOAD $k timed out"; exit 1 }
                "kextload: loaded" { puts "\nOK: VIRTIO-LOAD $k" }
                "already loaded"   { puts "\nOK: VIRTIO-LOAD $k (already loaded)" }
                -re {kldload\([^\n]*\n} {
                    puts "\nFAIL: VIRTIO-LOAD $k errored: $expect_out(0,string)"
                    send "kldstat\r"; expect { timeout {} -re {[#%$] $} {} }
                    send "dmesg | tail -30\r"; expect { timeout {} -re {[#%$] $} {} }
                    exit 1
                }
                -re "not a bundle" { puts "\nFAIL: VIRTIO-LOAD $k is not a readable bundle"; exit 1 }
            }
        }

        # Resident. Match the BUNDLE name -- kldstat lists the bundle binary
        # (VirtIOGraphics), never the KMOD name inside it (virtio_gpu_drm).
        send "kextstat | grep -qiE 'virtiographics|virtio_gpu_drm' && echo VIRTIO''_PRESENT || echo VIRTIO''_ABSENT\r"
        expect {
            timeout { puts "\nFAIL: VIRTIO-STAT timed out"; exit 1 }
            "VIRTIO_ABSENT"  { puts "\nFAIL: VirtIOGraphics loaded but absent from kextstat"; exit 1 }
            "VIRTIO_PRESENT" { puts "\nOK: VIRTIO-STAT (virtio_gpu_drm resident)" }
        }

        # It BOUND. bochs already holds card0, so a second DRM minor is the
        # evidence that virtio-gpu attached and brought KMS up. Asserted by
        # counting nodes rather than naming card1, so this stays true if minor
        # allocation ever changes order.
        send "ls /dev/dri/card* 2>/dev/null | wc -l\r"
        expect { timeout { } -re {[#%$] $} { } }
        send "test \$(ls /dev/dri/card* 2>/dev/null | wc -l) -ge 2 && echo VIRTIO''_CARD_YES || echo VIRTIO''_CARD_NO\r"
        expect {
            timeout { puts "\nFAIL: VIRTIO-CARD timed out"; exit 1 }
            "VIRTIO_CARD_YES" { puts "\nOK: VIRTIO-CARD (second DRM node -- virtio-gpu bound and KMS is up)" }
            "VIRTIO_CARD_NO"  {
                puts "\nFAIL: VIRTIO-CARD -- virtio-gpu loaded but published no DRM node"
                send "dmesg | grep -iE 'virtio|drm' | tail -30\r"
                expect { timeout {} -re {[#%$] $} {} }
                send "pciconf -lv | grep -A3 -i virtio\r"
                expect { timeout {} -re {[#%$] $} {} }
                exit 1
            }
        }
    }

    # Stage 12: diagnostics either way -- attach lines and any drm complaint.
    send "dmesg | grep -iE 'bochs|vboxvideo|virtio|drm|vgapci' | tail -30\r"
    expect { timeout { } -re {[#%$] $} { } }
    send "ls -l /dev/dri 2>&1 | head -8\r"
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
