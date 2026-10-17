# o2
Minimal xv6-like kernel for arm64 and riscv64.

## Build and run

    make                 # arm64: build/arm64/kernel.elf
    make qemu            # boot it in qemu-system-aarch64
    make ARCH=riscv64    # riscv64: build/riscv64/kernel.elf
    make ARCH=riscv64 qemu
    make CPUS=8 qemu     # number of CPUs (default 3)
    make SELFTEST=1 qemu # with the kernel self-tests at boot

The kernel boots into xv6's shell; programs live in `/` (`ls`, `cat`,
`echo`, `grep`, `wc`, `mkdir`, `rm`, `ln`, `kill`, `ps`, `pwd`, `usertests`).
^P on the console prints the kernel's process list.
`fs.img` persists between runs: `rm build/<arch>/fs.img` to start over.
`tools/run.sh` boots non-interactively for testing (see its header).

Needs the `aarch64-linux-gnu-` / `riscv64-linux-gnu-` toolchains and
QEMU (`qemu-system-arm`, `qemu-system-misc`). riscv64 runs in S-mode
under the OpenSBI firmware QEMU loads by default.

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
code runs, and the trampoline vectors switch it back on entry. Both
flush the TLB on the way out, since there are no ASIDs yet.

## Credits

Much of the arch-independent kernel (file system, log, buffer cache,
pipes, process management, system calls, virtio disk driver) and the
user programs and `mkfs` are ported from MIT's
[xv6-riscv](https://github.com/mit-pdos/xv6-riscv), which is under the
MIT license.
