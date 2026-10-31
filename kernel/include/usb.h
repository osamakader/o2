#ifndef USB_H
#define USB_H

#include "types.h"

// standard requests and descriptors
#define USB_GET_DESCRIPTOR    6
#define USB_SET_CONFIGURATION 9
#define USB_DESC_DEVICE    1
#define USB_DESC_CONFIG    2
#define USB_DESC_INTERFACE 4
#define USB_DESC_ENDPOINT  5

#define USB_CLASS_HID 3
#define USB_CLASS_HUB 9

// HID: class requests, and the boot interfaces' protocols
#define HID_SET_IDLE     0x0a
#define HID_SET_PROTOCOL 0x0b
#define HID_KEYBOARD 1
#define HID_MOUSE    2

// a HID boot interface's state (usbhid.c)
struct hid {
    int proto;          // HID_KEYBOARD or HID_MOUSE
    uchar last[8];      // the last report
};

void hid_init(struct hid *h, int proto);
void hid_report(struct hid *h, uchar *r, int n);
void xhci_init(void);

#endif
