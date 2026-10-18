#ifndef PLATFORM_H
#define PLATFORM_H

// interrupt controllers o2 drives, as the device tree calls them:
// board.intc_kind is the index of the one found
#define INTC_COMPATS { "arm,gic-v3", "arm,gic-400", "arm,cortex-a15-gic" }

#ifdef PLATFORM_RPI4

// Raspberry Pi 4 (BCM2711; qemu -machine raspi4b), in the "low
// peripheral" layout the firmware sets up: the peripherals are at
// 0xfc000000-0xffffffff, the GIC-400 at 0xff840000. Plain constants
// only: included from assembly via memlayout.h. The device tree says
// the same; these are for booting without one.

#include "param.h"

#define UART0     0xfe201000   // PL011 (uart0)
#define UART_CLOCK 0           // the firmware set its baud rate: keep it
#define EARLY_UART UART0       // so entry.S can write to it right away
#define UART0_IRQ (32 + 121)   // SPI 121
#define RTC0      0            // no real-time clock

// GIC-400 (GICv2): distributor and CPU interface
#define GICD      0xff841000
#define GICD_SIZE 0x1000
#define GICC      0xff842000
#define GICC_SIZE 0x2000

#define VIRTIO_MMIO        0
#define VIRTIO_MMIO_STRIDE 0
#define VIRTIO_MMIO_COUNT  0
#define VIRTIO_MMIO_IRQ0   0

#define UART_COMPAT "arm,pl011"
#define RTC_COMPAT  "none"
#define INTC_KIND   1   // the GIC-400 (INTC_COMPATS)
#define INTC_BASE   GICD
#define INTC_SIZE   GICD_SIZE
#define INTC2_BASE  GICC
#define INTC2_SIZE  GICC_SIZE

// RAM starts at 0; the firmware loads the kernel at kernel_address
// (config.txt), qemu at the ELF file's address.
#define KERNBASE    0
#define KERNEL_LOAD 0x200000

// the boot page table: 1GB of RAM, and the peripherals' 1GB block
#define BOOT_RAM_GB    1
#define BOOT_DEVICE_GB 3

#else

// qemu -machine virt (arm64) physical layout. Plain constants only:
// included from assembly via memlayout.h.

#include "param.h"

// qemu puts UART registers here in physical memory.
#define UART0 0x9000000
#define UART_CLOCK 24000000    // the PL011's reference clock
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
#define INTC_KIND   0   // a GICv3 (INTC_COMPATS)
#define INTC_BASE   GICD
#define INTC_SIZE   GICD_SIZE
#define INTC2_BASE  GICR
#define INTC2_SIZE  GICR_SIZE

// RAM starts here; qemu puts the device tree here and the kernel at
// 0x40200000 (KERNEL_LOAD, given to kernel.ld).
#define KERNBASE    0x40000000
#define KERNEL_LOAD 0x40200000

// the boot page table: the devices' 1GB, and all of RAM (MAX_RAM)
#define BOOT_RAM_GB    16
#define BOOT_DEVICE_GB 0

#endif

#endif
