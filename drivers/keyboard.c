#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/io.h>
#include <kernel/tty.h>
#include <kernel/proc.h>

static const char map[128] = {
    0,    27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    '\n', 0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4',  '5',
    '6',  '+',  '1', '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    0,    0,   0,   0,   0};

static const char map_shift[128] = {
    0,    27,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    '\n', 0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,    0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5',
    '6',  '+',  '1', '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,
    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,    0,   0,   0,   0};

static char buf[256];
static volatile int h = 0, t = 0;
static int shift_pressed = 0;
static int ctrl_pressed = 0;

void kbd_init(void) { h = t = 0; }

void keyboard_irq(void) {
  uint8_t sc = inb(0x60);
  outb(0x20, 0x20);

  if (sc & 0x80) {
    sc &= 0x7F;
    if (sc == 0x2A || sc == 0x36) shift_pressed = 0;
    if (sc == 0x1D) ctrl_pressed = 0;
    return;
  }

  if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return; }
  if (sc == 0x1D) { ctrl_pressed = 1; return; }

  char c = shift_pressed ? map_shift[sc] : map[sc];
  if (c == 0) return;
  if (c == '\r') c = '\n';

  if (ctrl_pressed && (c == 'c' || c == 'C')) {
    extern void send_signal(int pid, int sig);
    if (current && current->pid > 0) {
      send_signal(current->pid, 2);
    }
    con_puts("^C\n");
    ctrl_pressed = 0;
    return;
  }

  extern struct tty *console_tty;
  if (console_tty) {
    tty_insert_flip_char(console_tty, c);
  } else {
    buf[t++ & 255] = c;
  }
}

int kbd_read(char *dst, int n) {
  int c = 0;
  while (c < n && h != t) {
    dst[c++] = buf[h++ & 255];
  }
  return c;
}

char kbd_getchar(void) {
  if (h == t) return 0;
  return buf[h++ & 255];
}
