#ifndef PLATFORM_H
#define PLATFORM_H

// qemu -machine virt (arm64) physical layout. Plain constants only:
// included from assembly via memlayout.h.

#include "param.h"

// qemu puts UART registers here in physical memory.
#define UART0 0x9000000
#define UART0_IRQ 33     // SPI 1

// qemu puts interrupt controller (GICv3) here.
#define GICv3 0x08000000

// GICv3 distributor, and one 128KB redistributor (RD + SGI frames)
// per CPU starting at GICR.
#define GICD      GICv3
#define GICD_SIZE 0x10000
#define GICR      (GICv3 + 0xA0000)
#define GICR_SIZE (NCPU * 0x20000)

// virtio-mmio transports: VIRTIO_MMIO_COUNT slots, VIRTIO_MMIO_STRIDE
// apart; slot i interrupts on VIRTIO_MMIO_IRQ0 + i (SPI 16 + i).
#define VIRTIO_MMIO        0x0a000000
#define VIRTIO_MMIO_STRIDE 0x200
#define VIRTIO_MMIO_COUNT  32
#define VIRTIO_MMIO_IRQ0   48

// RAM starts here; qemu loads the kernel at 0x40080000 and puts the
// DTB below it.
#define KERNBASE 0x40000000

#endif
