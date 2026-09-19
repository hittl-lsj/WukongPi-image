# WuKong Pi H3 Buildroot Image

This repository contains the Buildroot external tree and helper script used to
build a minimal Linux image for the WuKong Pi H3 board.

The board uses an Allwinner H3 (ARM Cortex-A7) with 512 MiB RAM and boots from
the TF card. The image includes U-Boot, Linux 6.12.3, a custom WuKong Pi H3
device tree, BusyBox, Dropbear SSH, an ext4 root filesystem, and a terminal
login banner.

## Repository layout

```text
wukongpi-br2/                 Buildroot external tree
  board/wukongpi-h3/          DTS, U-Boot fragment, genimage and overlay
  configs/wukongpi_h3_defconfig
flash-wukongpi.sh             Safe removable-disk detection and flashing
```

Buildroot itself and its downloads are intentionally not committed. They are
large generated or third-party trees.

## Build

Download and extract the matching Buildroot release in the project directory:

```bash
wget https://buildroot.org/downloads/buildroot-2026.02.3.tar.xz
tar -xf buildroot-2026.02.3.tar.xz
```

Configure and build from the project directory:

```bash
cd buildroot-2026.02.3
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  wukongpi_h3_defconfig
```

Before a public or production build, open `menuconfig` and set a private root
password or configure an SSH public key. Passwords are deliberately not stored
in this repository.

```bash
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  menuconfig

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  -j"$(nproc)"
```

The finished image is:

```text
output-wukongpi/images/sdcard.img
```

## Flash a TF card

Insert the TF card and first perform a read-only detection:

```bash
./flash-wukongpi.sh --dry-run
```

Then flash it:

```bash
./flash-wukongpi.sh
```

The script only offers writable USB/removable disks, excludes the disk that
contains the running system and the image file, shows the detected model and
mounts, and requires typing the complete device path before writing.

## First boot

Use a 3.3 V USB-TTL adapter at 115200 8N1. The serial console is `ttyS0`.
The image boots from `/dev/mmcblk0p1` and provides a Dropbear SSH server.

For a direct Ethernet connection to a computer, assign matching temporary
addresses during development:

```text
WuKong Pi: 10.20.30.2/24
Computer: 10.20.30.1/24
```

Then connect with:

```bash
ssh root@10.20.30.2
```

## License

The board-specific files and helper script are released under the MIT License.
Buildroot and other third-party components retain their own licenses.
