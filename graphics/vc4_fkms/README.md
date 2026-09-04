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

The node ships disabled, so nothing happens without the overlay:

```
ofwbus0: <firmwarekms> irq 12 disabled compat raspberrypi,rpi-firmware-kms-2712 (no driver attached)
```

**1. Install the overlay.** On the FAT boot partition (`nda0s1` on an NVMe
install, `da0s1` on a card):

```sh
mkdir -p /mnt/boot && mount_msdosfs /dev/nda0s1 /mnt/boot
mkdir -p /mnt/boot/overlays
cp nextbsd-fkms.dtbo /mnt/boot/overlays/
cp /mnt/boot/config.txt /mnt/boot/config.nofkms      # one-line rollback
printf 'dtoverlay=nextbsd-fkms\n' >> /mnt/boot/config.txt
umount /mnt/boot
```

**2. Reboot and check the node is live.** Before loading anything:

```sh
dmesg | grep -i firmwarekms
```

`disabled` should be gone. If it still says `disabled`, the overlay did not
apply and nothing after this will work — check that `overlays/` is on the same
partition as `config.txt`.

**3. Load the driver.**

```sh
kldload drm_dma_helpers
kldload vc4_fkms
dmesg | tail -20
```

Expect `vc4_fkms0: <VideoCore firmware KMS>` and a line reporting the CRTC
count. `/dev/dri/card0` should appear.

**4. What success looks like.**

```sh
ls -l /dev/dri/
```

That device node is the whole point: X's `modesetting` driver binds to it
instead of falling back to `scfb`, which is what nextbsd#425 (scfb segfaults
at 16bpp) and gershwin-windowmanager#70 (software compositing saturating
Xorg) both come back to.

**Rollback** is `cp /mnt/boot/config.nofkms /mnt/boot/config.txt` and reboot.
The driver is a module, so not loading it is also a rollback.

## Honest state

Nothing above has run on hardware yet. The order in which this can fail, and
none of it is knowable from a build:

1. the module may not link — `vc4_firmware_kms.c` calls vc4 helpers that live
   in files not vendored here
2. the driver may not attach — the node is at the device tree root, so it
   attaches on `ofwbus` rather than `simplebus`
3. the firmware may not answer `RPI_FIRMWARE_FRAMEBUFFER_GET_NUM_DISPLAYS`
   and the rest of what fkms asks for on BCM2712
4. it may attach and register `/dev/dri/card0` and still not set a mode

Each step is worth reporting separately, because each one is a different
problem.
