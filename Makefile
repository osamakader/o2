ARCH ?= arm64

# Everything is built under build/$(ARCH), so both arches can be built
# from the same tree without cleaning in between.
B = build/$(ARCH)
KERNEL = $(B)/kernel.elf
# file system image for the virtio block device, made by mkfs
FSIMG = $(B)/fs.img
MKFS = $(B)/mkfs

KOBJS = \
  kernel/start.o \
  kernel/main.o \
  kernel/console.o \
  kernel/clock.o \
  kernel/irq.o \
  kernel/string.o \
  kernel/kalloc.o \
  kernel/proc.o \
  kernel/spinlock.o \
  kernel/sleeplock.o \
  kernel/selftest.o \
  kernel/vm.o \
  kernel/exec.o \
  kernel/virtio_disk.o \
  kernel/bio.o \
  kernel/log.o \
  kernel/fs.o \
  kernel/syscall.o \
  kernel/sysproc.o \
  kernel/sysfile.o \
  kernel/initcode.o \

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2

ifeq ($(ARCH), riscv64)
QEMU = qemu-system-riscv64
TOOLPREFIX = riscv64-linux-gnu-
# no floating point in the kernel (like -mgeneral-regs-only on arm64);
# medany keeps addressing PC-relative, so code runs both at its
# physical and its high-half address.
CFLAGS += -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax
AOBJS = \
  entry.o \
  trapvec.o \
  trap.o \
  swtch.o \
  uart.o \
  mmu.o \
  sbi.o \
  timer.o \
  plic.o \

QEMUOPTS = -machine virt -bios default
else
QEMU = qemu-system-aarch64
TOOLPREFIX = aarch64-linux-gnu-
CFLAGS += -march=armv8-a -mgeneral-regs-only -mno-outline-atomics
AOBJS = \
  entry.o \
  exceptions.o \
  swtch.o \
  irq.o \
  uart.o \
  gicv3.o \
  mmu.o \
  psci.o \
  trap.o \
  timer.o \

QEMUOPTS = -machine virt,gic-version=3 -cpu cortex-a57
endif

OBJS = $(addprefix $(B)/arch/$(ARCH)/, $(AOBJS)) $(addprefix $(B)/, $(KOBJS))

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

all: $(KERNEL) $(FSIMG)

$(KERNEL): $(OBJS) arch/$(ARCH)/kernel.ld
	$(LD) $(LDFLAGS) -T arch/$(ARCH)/kernel.ld -o $@ $(OBJS)
	$(OBJDUMP) -S $@ > $(B)/kernel.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(B)/kernel.sym

$(B)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -g -c -o $@ $<

$(B)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -g -c -o $@ $<

-include $(shell find $(B) -name '*.d' 2>/dev/null)

# initcode.S embeds the init program with .incbin, which -MD doesn't
# track.
$(B)/kernel/initcode.o: $(B)/user/_init
$(B)/kernel/initcode.o: CFLAGS += -DINITCODE='"$(B)/user/_init"'

# User programs: linked at VA 0 against a tiny libc (ulib, printf, and
# the arch's syscall stubs).
ULIB = $(B)/user/ulib.o $(B)/user/$(ARCH)/usys.o $(B)/user/printf.o

$(B)/user/_%: $(B)/user/%.o $(ULIB) user/$(ARCH)/user.ld
	$(LD) $(LDFLAGS) -T user/$(ARCH)/user.ld -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $(B)/user/$*.asm

.PRECIOUS: $(B)/user/%.o

clean:
	rm -rf build

ifndef CPUS
CPUS := 3
endif

# mkfs runs on the host
$(MKFS): mkfs/mkfs.c kernel/include/fs.h kernel/include/param.h kernel/include/stat.h
	@mkdir -p $(dir $@)
	gcc -Wall -Werror -I kernel/include -o $@ mkfs/mkfs.c

# programs installed in the file system's root directory
UPROGS = \
  $(B)/user/_init \

$(FSIMG): $(MKFS) README.md $(UPROGS)
	$(MKFS) $@ README.md $(UPROGS) > /dev/null

QEMUOPTS += -kernel $(KERNEL) -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=$(FSIMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: $(KERNEL) $(FSIMG)
	$(QEMU) $(QEMUOPTS)

# the QEMU command line, for tools/run.sh
print-qemu:
	@echo $(QEMU) $(QEMUOPTS)

.PHONY: all clean qemu print-qemu
