#ifndef INPUT_H
#define INPUT_H

#include "types.h"

// The input device ("input", major INPUT), shared with user space:
// keyboard and pointer events, Linux's codes (evdev), from whichever
// drivers found keyboards and mice (virtio_input.c). read() returns
// whole events; it waits for one unless O_NONBLOCK.

#define INPUT 3   // device major number

struct input_event {
    uint16 type;
    uint16 code;
    int value;
};

#define EV_SYN 0x00   // end of a group of events
#define EV_KEY 0x01   // code: a key or button; value 1 down, 0 up, 2 repeat
#define EV_REL 0x02   // code: REL_*; value: movement
#define EV_ABS 0x03   // code: ABS_*; value: position, 0..INPUT_ABS_MAX

#define REL_X     0x00
#define REL_Y     0x01
#define REL_WHEEL 0x08
#define ABS_X     0x00
#define ABS_Y     0x01

#define INPUT_ABS_MAX 32767   // absolute positions are scaled to this

#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

#endif
