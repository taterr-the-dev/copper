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
static int e0_pressed = 0;

void kbd_init(void) { h = t = 0; }

void keyboard_irq(void) {
  uint8_t sc = inb(0x60);
  outb(0x20, 0x20);

  if (sc == 0xE0) {
    e0_pressed = 1;
    return;
  }

  if (sc & 0x80) {
    sc &= 0x7F;
    if (sc == 0x2A || sc == 0x36) shift_pressed = 0;
    if (sc == 0x1D) ctrl_pressed = 0;
    if (sc == 0xE0) e0_pressed = 0;
    return;
  }

  if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return; }
  if (sc == 0x1D) { ctrl_pressed = 1; return; }

  if (e0_pressed) {
    e0_pressed = 0;
    const char *seq = NULL;
    if (sc == 0x48) seq = "\x1b[A";
    else if (sc == 0x50) seq = "\x1b[B";
    else if (sc == 0x4B) seq = "\x1b[D";
    else if (sc == 0x4D) seq = "\x1b[C";
    else if (sc == 0x47) seq = "\x1b[H";
    else if (sc == 0x4F) seq = "\x1b[F";
    else if (sc == 0x53) seq = "\x1b[3~";

    if (seq) {
      extern struct tty *console_tty;
      if (console_tty) {
        for (int i = 0; seq[i] != '\0'; i++) {
          tty_insert_flip_char(console_tty, seq[i]);
        }
      }
      return;
    }
  }

  char c = shift_pressed ? map_shift[sc] : map[sc];
  if (c == 0) return;
  if (c == '\r') c = '\n';

  if (ctrl_pressed) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
    else if (c >= 'A' && c <= 'Z') c = c - 'A' + 1;

    if (c == 3) {
      extern void send_signal(int pid, int sig);
      if (current && current->pid > 0) {
        send_signal(current->pid, 2);
      }
      con_puts("^C\n");
      ctrl_pressed = 0;
      return;
    }
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
