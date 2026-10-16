#ifndef PSCI_H
#define PSCI_H

#include "types.h"

#define PSCI_SUCCESS            0
#define PSCI_INVALID_PARAMETERS (-2)
#define PSCI_ALREADY_ON         (-4)

long psci_cpu_on(uint64 mpidr, uint64 entry);

#endif
