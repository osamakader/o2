// power: the desktop's shut down / restart dialog. Enter shuts down,
// r restarts, Escape cancels.

#include "types.h"
#include "user.h"
#include "syscall.h"
#include "win.h"

#define W     320
#define H     104
#define BTN_W 88
#define BTN_H 26

#define C_BG     0xf6f6f4
#define C_TEXT   0x202428
#define C_BUTTON 0xe0e2e6
#define C_OFF    0xc04040

static struct {
  char *label;
  int how;   // REBOOT_*, or -1: cancel
  int x;
} buttons[] = {
  { "Shut down", REBOOT_OFF },
  { "Restart", REBOOT_RESTART },
  { "Cancel", -1 },
};
#define NBUTTON 3
#define BTN_Y (H - BTN_H - 12)

static void
draw(void)
{
  win_fill(0, 0, win_w, win_h, C_BG);
  win_text(16, 20, "Shut down or restart o2?", C_TEXT);
  int x = W - 12;
  for(int i = NBUTTON - 1; i >= 0; i--){
    x -= BTN_W;
    buttons[i].x = x;
    win_fill(x, BTN_Y, BTN_W, BTN_H, i == 0 ? C_OFF : C_BUTTON);
    int n = strlen(buttons[i].label);
    win_text(x + (BTN_W - n * FONT_W) / 2, BTN_Y + 5, buttons[i].label, i == 0 ? 0xffffff : C_TEXT);
    x -= 8;
  }
  win_update(0, 0, win_w, win_h);
}

static void
act(int how)
{
  if(how < 0)
    exit(0);
  win_title(how == REBOOT_OFF ? "Shutting down" : "Restarting");
  reboot(how);
  exit(1);
}

int
main(void)
{
  int w, h;
  win_screen(&w, &h);
  if(win_open(W, H, "Power") < 0)
    exit(1);
  draw();

  struct winmsg m;
  int r;
  while((r = win_event(&m, 1)) >= 0){
    if(r == 0)
      continue;
    if(m.type == WE_CLOSE)
      break;
    if(m.type == WE_KEY){
      if(m.a == '\n' || m.a == '\r')
        act(REBOOT_OFF);
      else if(m.a == 'r')
        act(REBOOT_RESTART);
      else if(m.a == '\033' || m.a == 'q')
        break;
    } else if(m.type == WE_MOUSE && m.b == MOUSE_DOWN && m.a == 1 &&
              m.y >= BTN_Y && m.y < BTN_Y + BTN_H){
      for(int i = 0; i < NBUTTON; i++)
        if(m.x >= buttons[i].x && m.x < buttons[i].x + BTN_W)
          act(buttons[i].how);
    }
  }
  exit(0);
}
