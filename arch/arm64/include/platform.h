#ifndef PLATFORM_H
#define PLATFORM_H

// qemu -machine virt (arm64) physical layout. Plain constants only:
// included from assembly via memlayout.h.

#include "param.h"

// qemu puts UART registers here in physical memory.
#define UART0 0x9000000
#define UART0_IRQ 33     // SPI 1

// PL031 real-time clock
#define RTC0 0x09010000

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

// What the device tree calls them (board.c), and which addresses
// stand for the interrupt controller: distributor, redistributors.
#define UART_COMPAT "arm,pl011"
#define RTC_COMPAT  "arm,pl031"
#define INTC_COMPAT "arm,gic-v3"
#define INTC_BASE   GICD
#define INTC_SIZE   GICD_SIZE
#define INTC2_BASE  GICR
#define INTC2_SIZE  GICR_SIZE

// RAM starts here; qemu puts the device tree here and the kernel at
// 0x40200000 (kernel.ld).
#define KERNBASE 0x40000000

// the boot page table maps all of RAM (MAX_RAM)
#define BOOT_RAM_GB 16

#endif
