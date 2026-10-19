ARCH ?= arm64

# Boot-time kernel self-tests (kernel/selftest.c): make SELFTEST=1
SELFTEST ?= 0

# arm64: per-process ASIDs instead of a TLB flush on every return to
# user mode: make ARM64_ASID=1. Off by default: QEMU's TLB isn't
# ASID-tagged and flushes on every ASID change, so under emulation the
# flush-per-return scheme is faster; on hardware ASIDs avoid flushing.
ARM64_ASID ?= 0

# arm64 machine: virt (qemu -machine virt) or rpi4 (Raspberry Pi 4;
# qemu -machine raspi4b)
PLATFORM ?= virt

# Graphics, off by default to keep o2 small: make GPU=1 adds the
# framebuffer (virtio-gpu on qemu virt, the firmware's on the Pi), the
# programs that draw (splash, gfx) and qemu's display device.
GPU ?= 0

# Everything is built under build/$(ARCH), plus -selftest / -asid /
# -rpi4 for those options, so both arches and all settings can be built
# from the same tree without cleaning in between.
B = build/$(ARCH)
ifeq ($(SELFTEST), 1)
B := $(B)-selftest
endif
ifeq ($(ARCH)$(ARM64_ASID), arm641)
B := $(B)-asid
endif
ifeq ($(ARCH)$(PLATFORM), arm64rpi4)
B := $(B)-rpi4
endif
ifeq ($(GPU), 1)
B := $(B)-gpu
endif
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
  kernel/backtrace.o \
  kernel/string.o \
  kernel/kalloc.o \
  kernel/proc.o \
  kernel/spinlock.o \
  kernel/sleeplock.o \
  kernel/vm.o \
  kernel/exec.o \
  kernel/virtio_disk.o \
  kernel/bio.o \
  kernel/log.o \
  kernel/fs.o \
  kernel/file.o \
  kernel/pipe.o \
  kernel/syscall.o \
  kernel/sysproc.o \
  kernel/signal.o \
  kernel/mmap.o \
  kernel/shm.o \
  kernel/fdt.o \
  kernel/board.o \
  kernel/virtio_net.o \
  kernel/net.o \
  kernel/socket.o \
  kernel/rpi_mbox.o \
  kernel/sdhci.o \
  kernel/disk.o \
  kernel/sysfile.o \
  kernel/initcode.o \

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2

ifeq ($(ARCH), riscv64)
QEMU = qemu-system-riscv64
TOOLPREFIX = riscv64-linux-gnu-
# no floating point in the kernel (like -mgeneral-regs-only on arm64);
# user programs get the full rv64gc and the hard-float ABI. medany keeps
# addressing PC-relative, so code runs both at its physical and its
# high-half address.
KARCHFLAGS = -march=rv64imac_zicsr_zifencei -mabi=lp64
UARCHFLAGS = -march=rv64gc -mabi=lp64d
CFLAGS += -mcmodel=medany -mno-relax
AOBJS = \
  entry.o \
  trapvec.o \
  trampoline.o \
  trap.o \
  swtch.o \
  uart.o \
  mmu.o \
  fpu.o \
  fpsave.o \
  sbi.o \
  timer.o \
  rtc.o \
  plic.o \

QEMUOPTS = -machine virt -bios default
else
QEMU = qemu-system-aarch64
TOOLPREFIX = aarch64-linux-gnu-
# no FP/SIMD in the kernel; user programs may use them
KARCHFLAGS = -march=armv8-a -mgeneral-regs-only
UARCHFLAGS = -march=armv8-a
CFLAGS += -mno-outline-atomics
AOBJS = \
  entry.o \
  exceptions.o \
  tramp.o \
  swtch.o \
  irq.o \
  uart.o \
  gic.o \
  gicv2.o \
  gicv3.o \
  mmu.o \
  fpu.o \
  fpsave.o \
  psci.o \
  trap.o \
  timer.o \
  rtc.o \

ifeq ($(PLATFORM), rpi4)
CFLAGS += -DPLATFORM_RPI4
# where the kernel is loaded (platform.h's KERNEL_LOAD)
KERNEL_LOAD = 0x200000
# qemu doesn't make a device tree for the Pi: the firmware's (GPL, from
# Linux, so fetched rather than kept here)
DTB = $(B)/bcm2711-rpi-4-b.dtb
QEMUOPTS = -machine raspi4b -dtb $(DTB)
# the raw image, as the Pi firmware loads it, and an SD card image with
# the file system
IMAGE = $(B)/kernel8.img
SDIMG = $(B)/sd.img
else
KERNEL_LOAD = 0x40200000
QEMUOPTS = -machine virt,gic-version=3 -cpu cortex-a57
endif
endif

ifeq ($(GPU), 1)
KOBJS += kernel/fb.o kernel/virtio_gpu.o kernel/rpi_fb.o
KOBJS += kernel/input.o kernel/virtio_input.o
CFLAGS += -DCONFIG_GPU
endif

ifeq ($(SELFTEST), 1)
KOBJS += kernel/selftest.o
CFLAGS += -DSELFTEST
endif

ifeq ($(ARCH)$(ARM64_ASID), arm641)
CFLAGS += -DARM64_ASID
endif

OBJS = $(addprefix $(B)/arch/$(ARCH)/, $(AOBJS)) $(addprefix $(B)/, $(KOBJS))

# kernel code is built without floating point (see fpu.h), user code
# with it
ARCHFLAGS = $(KARCHFLAGS)
CFLAGS += $(ARCHFLAGS)
$(B)/user/%.o: ARCHFLAGS = $(UARCHFLAGS)

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
KLDFLAGS = $(LDFLAGS) --defsym=KERNEL_LOAD=$(or $(KERNEL_LOAD),0)

# Objects depend on $(B)/flags, which is rewritten whenever the compiler
# flags change, so a flag change rebuilds everything.
FLAGS := $(CC) $(CFLAGS) $(UARCHFLAGS)
ifneq ($(FLAGS),$(file < $(B)/flags))
$(shell mkdir -p $(B))
$(file > $(B)/flags,$(FLAGS))
endif

all: $(KERNEL) $(IMAGE) $(FSIMG) $(SDIMG) $(DTB)

# The kernel carries its own symbol table, for backtraces. Link once
# with an empty table, generate ksyms.S from that kernel's text
# symbols, and link again with it. ksyms goes last, in .rodata, so
# function addresses can't move between the two links; the build checks
# they didn't.
NM = $(TOOLPREFIX)nm
KSYMS_GEN = $(NM) -n $(1) | awk ' \
  BEGIN { print ".section .rodata"; print ".balign 8"; print ".global ksyms"; print "ksyms:" } \
  $$2 ~ /^[tT]$$/ && $$3 !~ /^\$$/ { n++; printf ".quad 0x%s, .Lname%d\n", $$1, n; names[n] = $$3 } \
  END { print ".global nksyms"; printf "nksyms: .quad %d\n", n; \
        print ".section .rodata.str1.1, \"aMS\", @progbits, 1"; \
        for (i = 1; i <= n; i++) printf ".Lname%d: .asciz \"%s\"\n", i, names[i] }'

$(B)/ksyms0.S:
	@mkdir -p $(dir $@)
	printf '.section .rodata\n.balign 8\n.global ksyms, nksyms\nksyms:\nnksyms: .quad 0\n' > $@

$(B)/kernel.nosyms.elf: $(OBJS) $(B)/ksyms0.o arch/$(ARCH)/kernel.ld
	$(LD) $(KLDFLAGS) -T arch/$(ARCH)/kernel.ld -o $@ $(OBJS) $(B)/ksyms0.o

$(B)/ksyms.S: $(B)/kernel.nosyms.elf
	$(call KSYMS_GEN,$<) > $@

$(B)/ksyms0.o $(B)/ksyms.o: $(B)/%.o: $(B)/%.S $(B)/flags
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL): $(OBJS) $(B)/ksyms.o arch/$(ARCH)/kernel.ld
	$(LD) $(KLDFLAGS) -T arch/$(ARCH)/kernel.ld -o $@ $(OBJS) $(B)/ksyms.o
	@$(NM) -n $(B)/kernel.nosyms.elf | awk '$$2 ~ /^[tT]$$/' > $(B)/text0.sym
	@$(NM) -n $@ | awk '$$2 ~ /^[tT]$$/' > $(B)/text1.sym
	@cmp -s $(B)/text0.sym $(B)/text1.sym || { echo "ksyms: text moved between links"; rm -f $@; exit 1; }
	$(OBJDUMP) -S $@ > $(B)/kernel.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(B)/kernel.sym

$(B)/%.o: %.S $(B)/flags
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -g -c -o $@ $<

$(B)/%.o: %.c $(B)/flags
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -g -c -o $@ $<

-include $(shell find $(B) -name '*.d' 2>/dev/null)

# initcode.S embeds the init program with .incbin, which -MD doesn't
# track.
$(B)/kernel/initcode.o: $(B)/user/_initcode
$(B)/kernel/initcode.o: CFLAGS += -DINITCODE='"$(B)/user/_initcode"'

# User programs: linked at VA 0 against a tiny libc (ulib, printf,
# malloc, and the arch's syscall stubs).
ULIB = $(B)/user/ulib.o $(B)/user/$(ARCH)/usys.o $(B)/user/printf.o $(B)/user/umalloc.o

$(B)/user/_%: $(B)/user/%.o $(ULIB) user/$(ARCH)/user.ld
	$(LD) $(LDFLAGS) -T user/$(ARCH)/user.ld -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $(B)/user/$*.asm

# desktop apps (GPU=1) draw their windows with libwin
WINAPPS = $(B)/user/_term $(B)/user/_files $(B)/user/_view
$(WINAPPS): $(B)/user/_%: $(B)/user/%.o $(B)/user/libwin.o $(ULIB) user/$(ARCH)/user.ld
	$(LD) $(LDFLAGS) -T user/$(ARCH)/user.ld -o $@ $< $(B)/user/libwin.o $(ULIB)
	$(OBJDUMP) -S $@ > $(B)/user/$*.asm

# xv6's own user programs (user/xv6), built unmodified: their
# kernel/*.h and user/user.h includes resolve through user/xv6/include.
$(B)/user/xv6/%.o: CFLAGS += -iquote user/xv6/include

# sqrtf() as an instruction, not a libm call that sets errno
$(B)/user/splash.o: CFLAGS += -fno-math-errno

$(B)/user/xv6/_%: $(B)/user/xv6/%.o $(ULIB) user/$(ARCH)/user.ld
	$(LD) $(LDFLAGS) -T user/$(ARCH)/user.ld -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $(B)/user/xv6/$*.asm

.PRECIOUS: $(B)/user/%.o $(B)/user/xv6/%.o

clean:
	rm -rf build

ifndef CPUS
CPUS := 3
endif
# RAM for qemu; the kernel finds out how much from the device tree
MEM ?= 128M
NET ?= 1

# mkfs runs on the host
$(MKFS): mkfs/mkfs.c kernel/include/fs.h kernel/include/param.h kernel/include/stat.h
	@mkdir -p $(dir $@)
	gcc -Wall -Werror -iquote kernel/include -o $@ mkfs/mkfs.c

# programs installed in the file system's root directory
UPROGS = \
  $(B)/user/_bench \
  $(B)/user/_cat \
  $(B)/user/_date \
  $(B)/user/_dmesg \
  $(B)/user/_echo \
  $(B)/user/_grep \
  $(B)/user/_init \
  $(B)/user/_kill \
  $(B)/user/_ln \
  $(B)/user/_lockstat \
  $(B)/user/_ls \
  $(B)/user/_mkdir \
  $(B)/user/_nettest \
  $(B)/user/_nslookup \
  $(B)/user/_ping \
  $(B)/user/_ps \
  $(B)/user/_pwd \
  $(B)/user/_rm \
  $(B)/user/_sh \
  $(B)/user/_o2tests \
  $(B)/user/xv6/_usertests \
  $(B)/user/xv6/_grind \
  $(B)/user/xv6/_stressfs \
  $(B)/user/_wc \

ifeq ($(GPU), 1)
UPROGS += $(B)/user/_gfx $(B)/user/_splash $(B)/user/_evtest $(B)/user/_desk $(WINAPPS)
endif

# README.md goes into the file system as README (xv6 tests read it)
$(B)/README: README.md
	@mkdir -p $(dir $@)
	cp $< $@

$(FSIMG): $(MKFS) $(B)/README $(UPROGS)
	$(MKFS) $@ $(B)/README $(UPROGS) > /dev/null

ifeq ($(ARCH)$(PLATFORM), arm64rpi4)
# raspi4b has 2GB and 4 CPUs, and no virtio devices. Given the raw
# image (a Linux Image to qemu), qemu starts it as the firmware does.
# The file system is on an SD card (tools/mksd.py).
QEMUOPTS += -kernel $(IMAGE) -nographic
QEMUOPTS += -drive file=$(SDIMG),if=sd,format=raw

$(SDIMG): $(FSIMG) tools/mksd.py
	python3 tools/mksd.py $(FSIMG) $@

$(IMAGE): $(KERNEL)
	$(OBJCOPY) -O binary $< $@

$(DTB):
	@mkdir -p $(dir $@)
	curl -sSfL -o $@ https://github.com/raspberrypi/firmware/raw/1.20260915/boot/bcm2711-rpi-4-b.dtb
else
QEMUOPTS += -kernel $(KERNEL) -m $(MEM) -smp $(CPUS) -nographic
# a virtio network card on qemu's user-mode network (NET=0: none); not
# -nic, whose PCI card wants a boot ROM that may not be installed
ifeq ($(NET), 0)
QEMUOPTS += -nic none
else
QEMUOPTS += -netdev user,id=net0 -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1
endif
# a virtio GPU (GPU=1); with -nographic nobody sees it, but the qemu
# monitor's screendump does
ifeq ($(GPU), 1)
QEMUOPTS += -device virtio-gpu-device,bus=virtio-mmio-bus.2
# and a keyboard and a tablet (an absolute pointer: the host's mouse)
QEMUOPTS += -device virtio-keyboard-device,bus=virtio-mmio-bus.3
QEMUOPTS += -device virtio-tablet-device,bus=virtio-mmio-bus.4
# and a relative mouse, like a real one: the monitor's mouse_move
# moves it (the tablet takes the GUI's pointer)
QEMUOPTS += -device virtio-mouse-device,bus=virtio-mmio-bus.5
endif
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=$(FSIMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
endif

qemu: $(KERNEL) $(IMAGE) $(FSIMG) $(SDIMG) $(DTB)
	$(QEMU) $(QEMUOPTS)

# the same with a window showing the framebuffer; the console stays here
qemu-gui: $(KERNEL) $(IMAGE) $(FSIMG) $(SDIMG) $(DTB)
	$(QEMU) $(subst -nographic,-serial mon:stdio,$(QEMUOPTS))

# the QEMU command line and disk image, for tools/run.sh
print-qemu:
	@echo $(QEMU) $(QEMUOPTS)

print-fsimg:
	@echo $(FSIMG)

.PHONY: all clean qemu print-qemu print-fsimg
