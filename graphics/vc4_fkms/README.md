# vc4_fkms — VideoCore firmware KMS for the Raspberry Pi 5

Mode setting through the VideoCore firmware's property mailbox, rather than by
driving the display hardware directly. Ported from `vc4_firmware_kms.c` in
`raspberrypi/linux` `rpi-6.12.y`.

Tracking issue: nextbsd-kernel#176.

## Why this and not vc4 proper

Full vc4 drives the HVS, the pixel valves and both HDMI blocks itself, and
binds them together with Linux's component framework over a device tree it
walks at runtime. Firmware KMS asks the firmware to do all of that over the
mailbox NextBSD already speaks. Measured on the 2079-line source, the
difference is stark:

```
clk_*                  0 calls    the firmware owns the pixel clock
component_add/del      2 calls    one device, nothing to wait for
rpi_firmware_*        11 calls    over bcm2835_firmware_property()
drm_*/drmm_*          53 distinct all present in drm-kmod 6.12-lts
```

## What is here

| file | role |
|---|---|
| `../drivers/gpu/drm/vc4/vc4_firmware_kms.c` | the driver, vendored unmodified |
| `vc4_fkms_freebsd.c` | `rpi_firmware_*` over the FreeBSD mailbox |
| `vc4_fkms_master.c` | the DRM master and the newbus `device_t` driver |
| `overlays/nextbsd-fkms.dts` | flips the `firmwarekms` node to `okay` |

## Requirements

- nextbsd-kernel#183 — LinuxKPI platform device, OF lookups, component framework
- nextbsd-kernel#181, #182 — the DMA GEM helper symbols
- nextbsd-kernel-extensions#42 — `drm_dma_helpers.ko`

## Testing on hardware

NextBSD has no `kldload` -- kexts are the delivery mechanism. Everything below
is `kextload` against bundles in `/System/Library/Extensions`.

You need two artifacts from the same CI run family:

| artifact | from | contains |
|---|---|---|
| `rpi5-kernel8-arm64.img` | nextbsd-kernel `continuous` | the kernel, with the linuxkpi work in #183/#184 |
| `graphics-kexts-arm64` | nextbsd-kernel-extensions | `VideoCoreKMS.kext` and its dependencies |

The kexts must come from a build against **that** kernel. A kext built against
a different one fails at load with ENOEXEC on unresolved symbols, which is the
check `Check every kext resolves` runs in CI for the PCI drivers and which this
bundle -- being loaded explicitly rather than matched -- does not get.

**1. Kernel.** Boot partition is `nda0s1` on an NVMe install, `da0s1` on a card:

```sh
mkdir -p /mnt/boot && mount_msdosfs /dev/nda0s1 /mnt/boot
cp /mnt/boot/kernel8.img /mnt/boot/kernel8.prev      # rollback
cp /path/to/rpi5-kernel8-arm64.img /mnt/boot/kernel8.img
```

**2. Overlay**, so the node stops being disabled:

```sh
mkdir -p /mnt/boot/overlays
cp nextbsd-fkms.dtbo /mnt/boot/overlays/
cp /mnt/boot/config.txt /mnt/boot/config.nofkms      # rollback
printf 'dtoverlay=nextbsd-fkms\n' >> /mnt/boot/config.txt
umount /mnt/boot
```

**3. Kexts.** Order does not matter -- `OSBundleLibraries` resolves the
dependency graph -- but all four must be present:

```sh
cd /System/Library/Extensions
tar xf /path/to/graphics-kexts-arm64.zip     # or copy the bundles in
chown -R root:wheel DMABuf.kext IOGraphics.kext IOGraphicsDMA.kext VideoCoreKMS.kext
chmod -R go-w      DMABuf.kext IOGraphics.kext IOGraphicsDMA.kext VideoCoreKMS.kext
```

Then reboot.

**4. Confirm the node is live**, before loading anything:

```sh
dmesg | grep -i firmwarekms
```

`disabled` should be gone. If it is still there the overlay did not apply and
nothing below will work -- check `overlays/` is on the same partition as
`config.txt`.

**5. Load it.**

```sh
kextload /System/Library/Extensions/VideoCoreKMS.kext
kextstat | grep -i videocore
dmesg | tail -20
```

`kextload: loaded` is the first real test of load-time symbol resolution --
CI proves the link, not the load. An ENOEXEC here means a symbol the kernel
does not have, and `dmesg` names it.

Expect `vc4_fkms0: <VideoCore firmware KMS>` and a line reporting the CRTC
count.

**6. What success looks like.**

```sh
ls -l /dev/dri/
```

That device node is the point: X's `modesetting` driver binds to it instead of
falling back to `scfb`, which is what nextbsd#425 (scfb segfaults at 16bpp) and
gershwin-windowmanager#70 (software compositing saturating Xorg) both come back
to.

**Rollback** is per layer: `cp /mnt/boot/config.nofkms /mnt/boot/config.txt`
for the overlay, `cp /mnt/boot/kernel8.prev /mnt/boot/kernel8.img` for the
kernel, and simply not loading the kext for the driver.

## Honest state

The module builds and links; nothing above has run on hardware. The order in
which this can fail, none of it knowable from a build:

1. the module may not link — `vc4_firmware_kms.c` calls vc4 helpers that live
   in files not vendored here
2. the driver may not attach — the node is at the device tree root, so it
   attaches on `ofwbus` rather than `simplebus`
3. the firmware may not answer `RPI_FIRMWARE_FRAMEBUFFER_GET_NUM_DISPLAYS`
   and the rest of what fkms asks for on BCM2712
4. it may attach and register `/dev/dri/card0` and still not set a mode

Each step is worth reporting separately, because each one is a different
problem.
