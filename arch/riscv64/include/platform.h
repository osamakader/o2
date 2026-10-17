#ifndef PLATFORM_H
#define PLATFORM_H

// qemu -machine virt (riscv64) physical layout. Plain constants only:
// included from assembly via memlayout.h.

#include "param.h"

// 16550a UART
#define UART0 0x10000000
#define UART0_IRQ 10

// platform-level interrupt controller (PLIC)
#define PLIC 0x0c000000
#define PLIC_SIZE 0x400000

// virtio-mmio transports: VIRTIO_MMIO_COUNT slots, VIRTIO_MMIO_STRIDE
// apart; slot i interrupts on PLIC source VIRTIO_MMIO_IRQ0 + i.
#define VIRTIO_MMIO        0x10001000
#define VIRTIO_MMIO_STRIDE 0x1000
#define VIRTIO_MMIO_COUNT  8
#define VIRTIO_MMIO_IRQ0   1

// RAM starts here. OpenSBI occupies the first 2MB; qemu loads the
// kernel at 0x80200000.
#define KERNBASE 0x80000000

// mtime frequency on qemu virt
#define TIMEBASE_FREQ 10000000

#endif
