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

The kernel boots into xv6's shell; programs live in `/` (`ls`, `cat`,
`echo`, `grep`, `wc`, `mkdir`, `rm`, `ln`, `kill [-sig]`, `ps`, `pwd`, `dmesg`, `date`), plus
xv6's `usertests`, `grind` and `stressfs` (`user/xv6`) and o2's own
tests, `o2tests`.
^P on the console prints the kernel's process list.
`fs.img` persists between runs: `rm build/<arch>/fs.img` to start over.
`tools/run.sh` boots non-interactively for testing (see its header), and
`tools/ci.sh <arch> [quick]` runs the whole test suite the way CI does
(GitHub Actions, `.github/workflows/ci.yml`, on every push).

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

Time comes from the RTC (PL031 on arm64, Goldfish on riscv64), read at
boot, plus the system counter; `date` shows it. Inodes are 128 bytes
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
line (left/right, home/end, backspace, delete, ^U), and survives ^C,
which interrupts the command it is running.

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
