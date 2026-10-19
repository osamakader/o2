#ifndef WIN_H
#define WIN_H

// Desktop apps (GPU=1). desk starts an app with three files open:
// events from desk, requests to desk, and the memory (memfd()) the
// app draws its window in: uint32 0x00RRGGBB pixels, the window's
// width a row. Both sides exchange struct winmsg, whole ones (pipe
// writes this small are atomic). libwin.c does it for the app.

#include "types.h"

#define WIN_EVFD  3   // events, from desk
#define WIN_REQFD 4   // requests, to desk
#define WIN_BUFFD 5   // the window's pixels, as big as the screen

struct winmsg {
  int type;
  int x, y, a, b;
  char s[44];
};

// requests
#define WR_SHOW   1   // a x b: the window's size; s: its title
#define WR_DAMAGE 2   // draw (x, y, a x b) of the window again
#define WR_TITLE  3   // s: a new title
#define WR_LAUNCH 4   // s: start this app (a command line)

// events
#define WE_KEY    1   // a: a character, or one of KEY_* below
#define WE_MOUSE  2   // x, y: where, in the window; a: the buttons
                      // down (1 left, 2 right, 4 middle); b: MOUSE_*
#define WE_CLOSE  3   // the close box: please exit
#define WE_FOCUS  4   // a: 1 the window has the keys now, 0 not
#define WE_SCREEN 5   // the first event: a x b, the biggest a window
                      // can be to fit on the screen

#define MOUSE_DOWN  1
#define MOUSE_UP    2
#define MOUSE_MOVE  3   // with a button down
#define MOUSE_WHEEL 4   // a: lines, up is positive

#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_HOME  0x104
#define KEY_END   0x105
#define KEY_PGUP  0x106
#define KEY_PGDN  0x107
#define KEY_DEL   0x108

// the font
#define FONT_W 8
#define FONT_H 16

// libwin.c
extern int win_w, win_h;
extern uint32 *win_px;
void win_screen(int *w, int *h);
int win_open(int w, int h, char *title);
void win_fill(int x, int y, int w, int h, uint32 c);
void win_char(int x, int y, int c, uint32 fg);
void win_text(int x, int y, char *s, uint32 fg);
void win_update(int x, int y, int w, int h);
void win_title(char *s);
void win_launch(char *cmd);
int win_event(struct winmsg *m, int wait);

#endif
