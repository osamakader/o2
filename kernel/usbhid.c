// USB keyboards and mice, in the HID boot protocol: their reports
// become input events (input.c), as the virtio ones' are.
//
// A keyboard reports the modifier keys as bits, then up to six keys
// held down (HID usage codes); keys come and go between reports. A
// mouse reports its buttons, then x and y moves and maybe the wheel's,
// as signed bytes.

#include "types.h"
#include "param.h"
#include "defs.h"
#include "input.h"
#include "usb.h"

// HID keyboard usage codes (0x04 to 0x65) to evdev key codes, as Linux
// maps them
static const uchar usage2key[0x66] = {
    0, 0, 0, 0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
    50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 2, 3,
    4, 5, 6, 7, 8, 9, 10, 11, 28, 1, 14, 15, 57, 12, 13, 26,
    27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 87, 88, 99, 70, 119, 110, 102, 104, 111, 107, 109, 106,
    105, 108, 103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
    72, 73, 82, 83, 86, 127,
};

// the modifier bits: left ctrl, shift, alt, meta, then the right ones
static const uchar mod2key[8] = { 29, 42, 56, 125, 97, 54, 100, 126 };

void hid_init(struct hid *h, int proto) {
    h->proto = proto;
    for (int i = 0; i < sizeof(h->last); i++)
        h->last[i] = 0;
}

static int has(const uchar *keys, int k) {
    for (int i = 2; i < 8; i++)
        if (keys[i] == k)
            return 1;
    return 0;
}

static void keyboard(struct hid *h, uchar *r, int n) {
    if (n < 8 || r[2] == 0x01)
        return;   // too short, or "too many keys down"
    uchar *old = h->last;
    for (int b = 0; b < 8; b++)
        if ((old[0] ^ r[0]) & (1 << b))
            input_report(EV_KEY, mod2key[b], (r[0] >> b) & 1);
    for (int i = 2; i < 8; i++) {
        if (old[i] >= 4 && old[i] < sizeof(usage2key) && !has(r, old[i]) && usage2key[old[i]])
            input_report(EV_KEY, usage2key[old[i]], 0);
        if (r[i] >= 4 && r[i] < sizeof(usage2key) && !has(old, r[i]) && usage2key[r[i]])
            input_report(EV_KEY, usage2key[r[i]], 1);
    }
    input_report(EV_SYN, 0, 0);
    for (int i = 0; i < 8; i++)
        old[i] = r[i];
}

static void mouse(struct hid *h, uchar *r, int n) {
    if (n < 3 || (r[0] == h->last[0] && r[1] == 0 && r[2] == 0 && (n < 4 || r[3] == 0)))
        return;   // nothing happened
    static const int buttons[3] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };
    for (int b = 0; b < 3; b++)
        if ((h->last[0] ^ r[0]) & (1 << b))
            input_report(EV_KEY, buttons[b], (r[0] >> b) & 1);
    if (r[1])
        input_report(EV_REL, REL_X, (signed char)r[1]);
    if (r[2])
        input_report(EV_REL, REL_Y, (signed char)r[2]);
    if (n >= 4 && r[3])
        input_report(EV_REL, REL_WHEEL, (signed char)r[3]);
    input_report(EV_SYN, 0, 0);
    h->last[0] = r[0];
}

// A report of n bytes came.
void hid_report(struct hid *h, uchar *r, int n) {
    if (h->proto == HID_KEYBOARD)
        keyboard(h, r, n);
    else
        mouse(h, r, n);
}
