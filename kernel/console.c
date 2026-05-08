#include "console.h"

void log_msg(const char *msg) {
    pl011_send(msg);
}
