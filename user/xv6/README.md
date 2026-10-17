# xv6 user programs

`usertests.c`, `grind.c` and `stressfs.c` are copied unmodified from
[xv6-riscv](https://github.com/mit-pdos/xv6-riscv) (commit `f780d78`,
2026-10-15; MIT license). They are built against o2's user library;
`include/` maps their `kernel/*.h` and `user/user.h` includes onto
o2's headers and supplies the few kernel layout constants they use,
with o2's values for each arch.
