// Powering off and rebooting: reboot(how) after committing the file
// system. The Raspberry Pi does it with its power-management block's
// watchdog, as Linux does (the firmware halts after a reset marked as
// partition 63); other machines through the firmware (arch_power()).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "board.h"
#include "syscall.h"
#include "timer.h"

#define PM_RSTC     0x1c
#define PM_RSTS     0x20
#define PM_WDOG     0x24
#define PM_PASSWORD 0x5a000000
#define PM_RSTC_WRCFG_CLR        0xffffffcf
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020
#define PM_RSTS_RASPBERRYPI_HALT 0x555   // partition 63: the firmware halts

static volatile uint32 *pm(int r)
{
    return (volatile uint32 *)(P2V(board.pm) + r);
}

static void rpi_power(int reboot)
{
    if (!reboot)
        *pm(PM_RSTS) = *pm(PM_RSTS) | PM_PASSWORD | PM_RSTS_RASPBERRYPI_HALT;
    *pm(PM_WDOG) = PM_PASSWORD | 10;   // ticks of ~16us
    *pm(PM_RSTC) = (*pm(PM_RSTC) & PM_RSTC_WRCFG_CLR) | PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
}

uint64 sys_sync(void);

// reboot(how): REBOOT_OFF powers off, REBOOT_RESTART restarts.
// Returns -1 only if that didn't work.
uint64 sys_reboot(void)
{
    int how;
    argint(0, &how);
    if (how != REBOOT_OFF && how != REBOOT_RESTART)
        return -1;
    sys_sync();
    printf("o2: %s\n", how == REBOOT_OFF ? "powering off" : "restarting");
    if (board.pm)
        rpi_power(how == REBOOT_RESTART);
    else
        arch_power(how == REBOOT_RESTART);
    for (uint64 t0 = ticks; __atomic_load_n(&ticks, __ATOMIC_RELAXED) - t0 < 100; )
        ;   // the watchdog takes a moment
    return -1;
}
