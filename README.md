# o2
Minimal xv6-like kernel for arm64 and riscv64.

## Build and run

    make                 # arm64: build/arm64/kernel.elf
    make qemu            # boot it in qemu-system-aarch64
    make ARCH=riscv64    # riscv64: build/riscv64/kernel.elf
    make ARCH=riscv64 qemu
    make CPUS=8 qemu     # number of CPUs (default 3)

The kernel boots into xv6's shell; programs live in `/` (`ls`, `cat`,
`echo`, `grep`, `wc`, `mkdir`, `rm`, `ln`, `kill`, `usertests`).
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

At boot the kernel runs self-tests (`kernel/selftest.c`); user-mode
tests are `usertests`, run from the shell.

## Credits

Much of the arch-independent kernel (file system, log, buffer cache,
pipes, process management, system calls, virtio disk driver) and the
user programs and `mkfs` are ported from MIT's
[xv6-riscv](https://github.com/mit-pdos/xv6-riscv), which is under the
MIT license.
