// xv6 include path -> o2 header
#include "../../../user.h"

// xv6's kill(pid) kills unconditionally
#define kill(pid) kill(pid, SIGKILL)
