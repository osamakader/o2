# o2
Minimal xv6-like kernel for arm64 and riscv64.

## Build and run

    make                 # arm64: build/arm64/kernel.elf
    make qemu            # boot it in qemu-system-aarch64
    make ARCH=riscv64    # riscv64: build/riscv64/kernel.elf
    make ARCH=riscv64 qemu
    make CPUS=8 qemu     # number of CPUs (default 3)
    make SELFTEST=1 qemu # with the kernel self-tests at boot
    make ARM64_ASID=1 qemu  # arm64: per-process ASIDs (see Isolation)
    make GPU=1 qemu      # with graphics: the framebuffer, splash, gfx, desk
    make GPU=1 qemu-gui  # ... and a window showing it
    make GPU=1 USB=1 qemu-gui  # ... with a USB keyboard and mouse

The kernel boots into xv6's shell; programs live in `/` (`ls`, `cat`,
`echo`, `grep`, `wc`, `mkdir`, `rm`, `ln`, `kill [-sig]`, `ps`, `pwd`, `dmesg`, `date`), plus
xv6's `usertests`, `grind` and `stressfs` (`user/xv6`) and o2's own
tests, `o2tests`.
^P on the console prints the kernel's process list.
`fs.img` persists between runs: `rm build/<arch>/fs.img` to start over.
`tools/run.sh` boots non-interactively for testing (see its header), and
`tools/ci.sh <arch> [quick]` runs the whole test suite the way CI does
(GitHub Actions, `.github/workflows/ci.yml`, on every push).

The machine is described by the firmware's device tree (FDT): RAM size
(`make MEM=1G qemu`; up to 16GB) and reserved memory, the CPUs, the
riscv64 timer frequency, and the UART, RTC, interrupt controller and
virtio-mmio devices with their interrupts. Without one, qemu virt's
layout is built in. On arm64 qemu puts the tree at the start of RAM,
which is why the kernel loads at 0x40200000.

Needs the `aarch64-linux-gnu-` / `riscv64-linux-gnu-` toolchains and
QEMU (`qemu-system-arm`, `qemu-system-misc`). riscv64 runs in S-mode
under the OpenSBI firmware QEMU loads by default.

## Raspberry Pi 4

`make PLATFORM=rpi4` builds o2 for the Pi 4 (arm64): `kernel8.img`
(the raw kernel, with an arm64 Image header) and `sd.img`, an SD card
image with the file system in a partition of type 0x6f. `make
PLATFORM=rpi4 qemu` runs it on qemu's raspi4b (qemu 9.0 or later),
which is how it is tested (`tools/ci.sh rpi4`).

What differs from qemu virt: RAM from 0, entry in EL2, a GIC-400
(GICv2), the CPUs started from the firmware's spin table, the SD card
(SDHCI) for storage, the firmware's framebuffer (mailbox) for the
screen, USB through the PCIe bridge (GPU=1), and no RTC or network.

On a real Pi 4 (tried on a Pi 4 Model B Rev 1.5: the shell, gfx and
the boot animation on HDMI work):

1. You need a micro SD card whose contents may go (all of it is
   erased), a 3.3V USB-serial adapter on the GPIO header (adapter RX
   to pin 8, GPIO 14; adapter TX to pin 10, GPIO 15; GND to pin 6),
   and on the host sudo, curl and mkfs.vfat (dosfstools). HDMI is
   optional: with GPU=1 it shows the boot animation.
2. Build: `make ARCH=arm64 PLATFORM=rpi4`, or with graphics `make
   ARCH=arm64 PLATFORM=rpi4 GPU=1` (and `GPU=1` before the next
   command too).
3. With the card in the host's reader, as /dev/sdX (the whole card;
   check with `lsblk`), back it up if anything on it matters
   (`sudo dd if=/dev/sdX of=card-backup.img bs=4M`), then
   `tools/rpi4-card.sh /dev/sdX`. It shows the card and asks you to
   type yes, unmounts it, writes `sd.img`, then formats the first
   partition FAT16 and copies the firmware (`start4.elf`,
   `fixup4.dat`, the device tree, the `disable-bt` overlay, all from
   the pinned firmware release), `tools/rpi4/config.txt` and
   `kernel8.img` to it.
4. Open the serial console at 115200 baud, 8N1, e.g. `picocom -b
   115200 /dev/ttyUSB0`, put the card in the Pi and power it on. The
   firmware loads `kernel8.img` at 0x200000 and starts it in EL2;
   o2 prints its boot messages, plays the animation on HDMI (GPU=1) and starts
   the shell on the serial console.

`config.txt` makes the PL011 (uart0) the console on GPIO 14/15
(`enable_uart=1`, `dtoverlay=disable-bt`); o2 keeps the baud rate the
firmware set. It also has the firmware log there (`uart_2ndstage=1`).
If nothing shows up, check the wiring (TX and RX cross) and that the
adapter is 3.3V. To see how far o2 got: it writes A, B, C to the UART
at entry, in EL1 and with the MMU on, before its own console works, and
an exception inside the KPTI trampoline prints `!` with ESR, ELR, FAR
and SPSR instead of hanging. Bringing it up on the real board found
what qemu doesn't model: a FAT32 boot partition too small for the
firmware to read, and the Cortex-A72's TLB walk cache, which kept the
trampoline's page table entries across the switch back to the kernel's.

The Pi 5 isn't supported yet; much of this (EL2 entry, GICv2, CPU
numbering, the device tree's ranges, SD cards) is meant to carry over.

## Layout

- `kernel/`: architecture-independent kernel
- `arch/arm64/`, `arch/riscv64/`: boot, MMU, traps, context switch,
  timer, interrupt controller (GICv3 / PLIC), UART (PL011 / 16550),
  CPU bring-up (PSCI / SBI HSM)
- `user/`: user library and programs; `user/<arch>/` holds the
  syscall stubs and linker script
- `mkfs/`: host tool that builds `fs.img`

With `SELFTEST=1` the kernel runs its self-tests (`kernel/selftest.c`)
at boot; user-mode tests are `usertests`, run from the shell.

## Isolation

User page tables don't map the kernel. On riscv64, as in xv6, each
maps only the process's memory, a trampoline page and its trap frame;
traps switch `satp` to the kernel page table in the trampoline. On
arm64 (KPTI), TTBR1 holds a table with just the trampoline while user
code runs, and the trampoline vectors switch it back on entry.

On riscv64, and on arm64 with `ARM64_ASID=1`, each process slot has its
own ASID and the kernel uses ASID 0, with all mappings non-global, so
crossings don't flush the TLB; an ASID is flushed on every hart when
its address space shrinks, is replaced by exec, or is freed. QEMU's TLB
isn't ASID-tagged and flushes on ASID changes itself, which makes the
arm64 ASID path slower under QEMU, so by default arm64 runs everything
under ASID 0 and flushes the TLB on each return to user mode instead.

## Beyond xv6

System calls xv6 doesn't have: `getppid`, `dup2`, `lseek` (not past
the end of a file, which xv6's write semantics rule out), `waitpid`
(with `WNOHANG`), `kill(pid, sig)`, `signal`, `clock_gettime` and
`ioctl` (the console's raw mode). Files starting with
`#!interpreter [arg]` are run by their interpreter.

fork() shares memory copy-on-write: writable pages become read-only in
both processes, marked by a software PTE bit, and the first write to
one, by the process or by the kernel for it, copies it (pages are
reference counted; the last writer keeps the original).

`mmap`/`munmap` map anonymous memory or files, MAP_PRIVATE or
MAP_SHARED, placed downward from the top of user space. Anonymous
private pages are allocated on first touch; shared ones, and files,
when mapped (shared mappings stay shared with children). Writable
shared file mappings are written back on munmap, exit and exec; with
no page cache, separate mappings of a file see each other's changes
only after that.

Files can be about 64MB (a double-indirect block), on a 70000-block
file system; symbolic links (`ln -s`, `symlink`, `readlink`, `lstat`,
`O_NOFOLLOW`) are followed anywhere in a path, up to 10 per lookup.
The log commits in groups: finished system calls collect until the log
is full, a second has passed, or `sync` is called.

Networking: a virtio-net driver and a small IPv4 stack (ARP, IPv4,
ICMP, UDP) on qemu's user-mode network, where o2 is 10.0.2.15, the host
10.0.2.2 and its DNS server 10.0.2.3 (`make NET=0` leaves the card
out). Datagram sockets are files: `socket`, `bind`, `connect`,
`sendto`, `recvfrom`, `setsockopt(SO_RCVTIMEO)`; ICMP sockets do
pings. `ping`, `nettest` (against `tools/udpecho.py` on the host) and
`nslookup` use them. No TCP yet.

Graphics, with `make GPU=1` (off by default, to keep o2 small; it
builds into build/<arch>-gpu): a framebuffer, virtio-gpu on qemu virt
(2D, 1280x800) or the firmware's on the Pi. At boot, init plays
`splash`, an animation of the o2 logo, before starting the shell. Programs mmap the `fb` device
and ioctl `FB_INFO` and `FB_FLUSH` (fb.h); `gfx` draws a test picture,
`gfx bounce` an animation. qemu runs without a window here; `tools/run.sh
-D shot.ppm` saves a screen dump through the monitor, and CI checks
gfx's picture in one. `make GPU=1 qemu-gui` opens a window showing it.

`desk`, also with GPU=1, is a small desktop: a window manager. It
draws the window frames (move them by the title bar, close them with
the box on it), a taskbar (buttons that start apps, one per window, a
clock) and the mouse pointer, and passes keys and clicks to the top
window. Apps are programs it starts with three files: a pipe of
events, a pipe of requests, and the window's pixels in shared memory
(`memfd()`); user/win.h is the protocol and user/libwin.c does it for
them. The apps:

- `term [command]`: a terminal running a shell through pipes. It edits
  the line itself: ^U, ^C (interrupts what its shell runs), ^D (ends
  the shell).
- `files [dir]`: a file explorer. Double-click or Enter opens: a folder
  goes in, a text file opens in `view`, a program runs (in a terminal,
  unless it is an app). Up or Backspace goes to the parent.
- `view file`: a text viewer; the wheel, arrows and Page Up/Down scroll.
- `cal`: a calendar, a month at a time with today marked; the
  taskbar's clock opens it too.
- `power`: shut down or restart (the taskbar's Power button).

Apps wait with `poll()` (on pipes, sockets, the console and the input
device, with a timeout), so an idle desktop doesn't use the CPU.

Keys and the mouse come from the `input` device: evdev-style events
(input.h; `evtest` prints them) from qemu's virtio keyboard, tablet
(the GUI's pointer) and mouse, or from USB keyboards and mice. Without
a keyboard, lines typed on the serial console go to the top window.

USB (with GPU=1): the PCI bus is scanned (qemu virt's ECAM host, or
the Pi 4's PCIe bridge, set up as Linux does) and an xHCI controller
on it started: qemu's `qemu-xhci` (`make GPU=1 USB=1`), the Pi 4's
VL805, whose firmware the Pi's firmware loads. A kernel thread, `usb`,
polls it every tick. Keyboards and mice plugged into its ports, also
later, are used in the HID boot protocol: no hubs (a keyboard with a
hub in it may not work), no key repeat. Rings and buffers are DMA
pages (dma.c), uncached on arm64 since the Pi's PCIe doesn't see the
CPU caches.
Ctrl-Alt-Backspace, or a line `~.` on the serial console, quits. The
font is Spleen 8x16 (BSD-2-Clause, user/font.h, made by
tools/bdf2c.py). `tools/run.sh -m file` sends monitor commands
(`sendkey`, `mouse_move`, `mouse_button`) to drive it in tests.

Time comes from the RTC (PL031 on arm64, Goldfish on riscv64), read at
boot, plus the system counter; `date` shows it and `date -s YYYY-MM-DD
HH:MM` sets it (`clock_settime()`). The Raspberry Pi has no RTC: init
starts its clock at the newest time a file in / was changed (when the
file system was built, or last written), which `date -s` then
corrects. The clock runs in UTC; `tz +HH:MM [name]` sets the time zone
(in /timezone; `tz UTC` removes it), which `date` (`-u` for UTC), `ls`
and the desktop show, through `localtime()`. There are no daylight
saving rules: set the new offset when it changes.

`halt` and `reboot` commit the file system and power off or restart:
through PSCI on arm64 qemu, the SBI on riscv64, and the power
management block's watchdog on the Pi (the firmware halts, as with
Linux). Inodes are 128 bytes
on disk and record atime (creation only: reads don't update it), mtime
and ctime, which `stat` returns and `ls` shows.

Signals use Linux's numbers. Most terminate by default, SIGCHLD (sent
to the parent when a child exits) is ignored, and SIGKILL can't be
caught. A handler runs on the process's own stack, in a frame holding
the interrupted registers and FP state, and returns through
`sigreturn`. Pending signals interrupt sleeping system calls, which
return -1. Faults raise SIGSEGV, SIGBUS, SIGILL or SIGTRAP, which a
handler may catch; ignoring them doesn't work. ^C on the console sends
SIGINT to every user process but init, since there are no process
groups.

The shell runs scripts (`sh file`, or `#!/sh`), skips `#` comments,
looks commands up in `PATH` (`PATH=/:/bin`; default `/`), keeps a
`history` (`!!`, `!n`, or the up and down keys), edits the command
line (left/right, home/end, backspace, delete, ^U), completes command
and file names with tab, and survives ^C, which interrupts the command
it is running.

## Scalability

`lockstat cmd` shows the spin locks a command contended on (acquire()
counts per lock name, per CPU), and `bench alloc|fs|fork [n]` times n
processes of a workload in parallel. Changes from xv6 it guided:

- the page allocator has a free list per CPU (taking pages from
  another's when empty), and atomic page reference counts;
- the scheduler and wakeup() look at process slots without their lock
  first, instead of locking all 64 on every pass;
- the buffer cache is 31 hash buckets with a lock each, and 1024
  buffers instead of 120;
- the log commits in groups, and writes the log and the home blocks
  as batches of virtio requests (up to 21 at a time).

On 4 CPUs under qemu, `bench fs 4` went from 3.3s to 0.45s, and
xv6's writebig (a 64MB file, 1KB per write) from 7.5 minutes to under
one. wait_lock, which serializes every exit and wait, is the main
contention left (`bench fork`).

## Floating point

User programs may use floating point and SIMD; the kernel itself is
built without them. FP state is switched lazily: a process's first FP
instruction traps (arm64: CPACR_EL1.FPEN, riscv64: sstatus.FS off), and
the kernel then loads its registers. A process that never uses FP
costs nothing, and one that does has its registers saved only when it
is switched out.

## Credits

Much of the arch-independent kernel (file system, log, buffer cache,
pipes, process management, system calls, virtio disk driver) and the
user programs and `mkfs` are ported from MIT's
[xv6-riscv](https://github.com/mit-pdos/xv6-riscv), which is under the
MIT license.
