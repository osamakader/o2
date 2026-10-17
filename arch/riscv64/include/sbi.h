#ifndef SBI_H
#define SBI_H

#include "types.h"

// SBI (Supervisor Binary Interface) calls into OpenSBI.

#define SBI_SUCCESS            0
#define SBI_ERR_INVALID_PARAM  (-3)
#define SBI_ERR_ALREADY_AVAILABLE (-6)

void sbi_set_timer(uint64 stime);
long sbi_hart_start(uint64 hartid, uint64 start_addr, uint64 opaque);
long sbi_remote_sfence_vma_asid_all(uint64 asid);

#endif
