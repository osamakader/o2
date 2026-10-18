#!/bin/sh
# rpi4-card.sh /dev/sdX: put o2 on an SD card for a Raspberry Pi 4.
# THIS ERASES THE CARD. It writes build/arm64-rpi4/sd.img (an MBR, an
# empty first partition, o2's file system in the second), then makes
# the first partition FAT32 with what the firmware needs: its files
# (from the raspberrypi/firmware release the build's device tree comes
# from), tools/rpi4/config.txt and kernel8.img. Needs sudo, curl and
# mkfs.vfat (dosfstools).
set -e

DEV=$1
if [ -z "$DEV" ] || [ ! -b "$DEV" ]; then
    echo "usage: tools/rpi4-card.sh /dev/sdX (the whole card, not a partition)" >&2
    exit 1
fi
cd "$(dirname "$0")/.."

B=build/arm64-rpi4
FW=https://github.com/raspberrypi/firmware/raw/1.20260915/boot
make -s ARCH=arm64 PLATFORM=rpi4

mkdir -p "$B/firmware/overlays"
for f in start4.elf fixup4.dat bcm2711-rpi-4-b.dtb overlays/disable-bt.dtbo; do
    [ -f "$B/firmware/$f" ] || curl -sSfL -o "$B/firmware/$f" "$FW/$f"
done

echo "This erases all of $DEV:"
lsblk -o NAME,SIZE,TRAN,MODEL,FSTYPE,LABEL,MOUNTPOINTS "$DEV"
printf 'Type yes to go on: '
read answer
[ "$answer" = yes ] || exit 1

# nothing on it may stay mounted
for p in $(lsblk -lno PATH "$DEV" | tail -n +2); do
    sudo umount "$p" 2>/dev/null || true
done

sudo dd if="$B/sd.img" of="$DEV" bs=4M conv=fsync status=progress
sudo partprobe "$DEV" 2>/dev/null || sudo blockdev --rereadpt "$DEV" || true
sleep 1
case "$DEV" in
*[0-9]) P1=${DEV}p1 ;;   # /dev/mmcblk0p1, /dev/nvme0n1p1
*)      P1=${DEV}1 ;;
esac
sudo mkfs.vfat -F 32 -n O2BOOT "$P1"

MNT=$(mktemp -d)
sudo mount "$P1" "$MNT"
sudo cp -r "$B"/firmware/* "$MNT"/
sudo cp tools/rpi4/config.txt "$B/kernel8.img" "$MNT"/
sudo umount "$MNT"
rmdir "$MNT"
sync
echo "done: put the card in the Pi 4; the console is the PL011 on GPIO 14/15, 115200 baud"
