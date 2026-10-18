#!/usr/bin/env python3
# mksd.py fs.img sd.img: an SD card image for the Raspberry Pi: an MBR
# with a 32MB FAT16 partition (type 0x0e, left empty here: the
# firmware's files go there on a real card; 32MB is too small for a
# proper FAT32, which the firmware then fails to read kernel8.img from)
# and the o2 file system in a partition
# of type 0x6f, which the kernel looks for (disk.c). The image is 128MB
# or the next power of two that holds it: qemu wants one.
import sys

SECTOR = 512
fs = open(sys.argv[1], "rb").read()
fat_start, fat_sectors = 2048, 32 * 2048
o2_start = fat_start + fat_sectors
o2_sectors = (len(fs) + SECTOR - 1) // SECTOR
size = 128 << 20
while size < (o2_start + o2_sectors) * SECTOR:
    size *= 2

def entry(ptype, start, count):
    # status, CHS start (unused), type, CHS end (unused), LBA start, count
    return (bytes([0, 0xfe, 0xff, 0xff, ptype, 0xfe, 0xff, 0xff]) +
            start.to_bytes(4, "little") + count.to_bytes(4, "little"))

mbr = bytearray(SECTOR)
mbr[446:462] = entry(0x0e, fat_start, fat_sectors)
mbr[462:478] = entry(0x6f, o2_start, o2_sectors)
mbr[510:512] = b"\x55\xaa"

with open(sys.argv[2], "wb") as f:
    f.write(mbr)
    f.seek(o2_start * SECTOR)
    f.write(fs)
    f.truncate(size)
