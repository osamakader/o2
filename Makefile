ARCH ?= arm64

OBJS = \
  arch/$(ARCH)/entry.o \
  arch/$(ARCH)/exceptions.o \
  arch/$(ARCH)/swtch.o \
  kernel/isr.o \
  kernel/start.o \
  kernel/console.o \
  arch/$(ARCH)/uart.o \
  arch/$(ARCH)/gicv3.o \
  kernel/timer.o \
  kernel/main.o \
  kernel/string.o \
  kernel/kalloc.o \
  kernel/proc.o \

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2

ifeq ($(ARCH), riscv64)
	QEMU = qemu-system-riscv64
	TOOLPREFIX = riscv64-linux-gnu-
	CFLAGS += -march=rv64gc
else
	QEMU = qemu-system-aarch64
	TOOLPREFIX = aarch64-linux-gnu-
	CFLAGS += -march=armv8-a
endif

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS += -MD
CFLAGS += -ffreestanding
CFLAGS += -fno-common -nostdlib
CFLAGS += -I. -I kernel/include -I arch/$(ARCH)/include
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

kernel.elf: $(OBJS) arch/$(ARCH)/kernel.ld
	$(LD) $(LDFLAGS) -T arch/$(ARCH)/kernel.ld -o kernel.elf $(OBJS) 
	$(OBJDUMP) -S kernel.elf > kernel.asm
	$(OBJDUMP) -t kernel.elf | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernel.sym

%.o: %.S
	$(CC) $(CFLAGS) -g -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -g -c -o $@ $<

-include arch/$(ARCH)/*.d
-include kernel/*.d

clean: 
	rm -rf *.log *.sym *.asm *.o \
	*/*.o */*.d */*.asm */*.sym \
	arch/*/*.o arch/*/*.d kernel.elf

ifndef CPUS
CPUS := 3
endif

QEMUOPTS = -machine virt,gic-version=3 -kernel kernel.elf -m 128M -smp $(CPUS) -nographic -cpu cortex-a57

qemu: kernel.elf
	$(QEMU) $(QEMUOPTS)
