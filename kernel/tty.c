#include <kernel/console.h>
#include <kernel/kmalloc.h>
#include <kernel/proc.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/tty.h>
#include <kernel/arch.h>

extern void yield(void);
static void copy_str(char *d, const char *s, size_t n) {
  size_t i = 0;
  for (; i + 1 < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = 0;
}

struct tty *tty_alloc(const char *name, struct tty_ops *ops) {
  struct tty *tty = kmalloc(sizeof(struct tty));
  if (!tty)
    return NULL;
  memset(tty, 0, sizeof(struct tty));
  copy_str(tty->name, name, 32);
  tty->ops = ops;
  tty->termios.c_iflag = ICRNL | IXON;
  tty->termios.c_oflag = OPOST | ONLCR;
  tty->termios.c_cflag = 0x00bf;
  tty->termios.c_lflag = ECHO | ICANON | ISIG;

  tty->termios.c_cc[VEOF] = 4;
  tty->termios.c_cc[VINTR] = 3;
  tty->termios.c_cc[VERASE] = 127;

  tty->winsize.ws_row = 24;
  tty->winsize.ws_col = 80;

  return tty;
}

void tty_free(struct tty *tty) {
  if (tty)
    kfree(tty);
}

void tty_insert_flip_char(struct tty *tty, char ch) {
  if (tty->termios.c_lflag & ICANON) {
    if (ch == tty->termios.c_cc[VERASE] || ch == '\b') {
      if (tty->read_count > 0) {
        tty->read_count--;
        tty->read_head = (tty->read_head - 1 + 4096) % 4096;
        if (tty->termios.c_lflag & ECHO) {
          char bs[] = "\b \b";
          if (tty->ops && tty->ops->write)
            tty->ops->write(tty, bs, 3);
        }
      }
      return;
    }
    if (ch == '\r' && (tty->termios.c_iflag & ICRNL)) {
      ch = '\n';
    }
  }

  if (tty->read_count < 4096) {
    tty->read_buf[tty->read_head] = ch;
    tty->read_head = (tty->read_head + 1) % 4096;
    tty->read_count++;
  }

  if (tty->termios.c_lflag & ECHO) {
    char out = ch;
    if (tty->ops && tty->ops->write)
      tty->ops->write(tty, &out, 1);
  }

  if (tty->read_waiter) {
    int should_wake = 0;
    if (tty->termios.c_lflag & ICANON) {
      if (ch == '\n')
        should_wake = 1;
    } else {
      should_wake = 1;
    }

    if (should_wake) {
      struct task *waiter = (struct task *)tty->read_waiter;
      waiter->state = 1;
      tty->read_waiter = NULL;
    }
  }
}

int tty_read(struct tty *tty, char *buf, size_t len) {
  size_t rd = 0;
  while (rd < len) {
    if (tty->termios.c_lflag & ICANON) {
      int has_newline = 0;
      int temp_tail = tty->read_tail;

      for (int i = 0; i < tty->read_count; i++) {
        if (tty->read_buf[temp_tail] == '\n') {
          has_newline = 1;
          break;
        }
        temp_tail = (temp_tail + 1) % 4096;
      }

      if (!has_newline) {
				arch_cli();
        tty->read_waiter = current;
        current->state = T_BLOCKED;
        schedule();
        tty->read_waiter = NULL;
				arch_sti();
        continue;
      }
    }

    if (tty->read_count == 0) {
			arch_cli();
      tty->read_waiter = current;
      current->state = T_BLOCKED;
      schedule();
      tty->read_waiter = NULL;
      arch_sti();
      continue;
    }

    char ch = tty->read_buf[tty->read_tail];
    tty->read_tail = (tty->read_tail + 1) % 4096;
    tty->read_count--;
    buf[rd++] = ch;

    if ((tty->termios.c_lflag & ICANON) && ch == '\n')
      break;
  }
  return rd;
}

int tty_write(struct tty *tty, const char *buf, size_t len) {
  if (!tty->ops || !tty->ops->write)
    return -EIO;

  if (tty->termios.c_oflag & OPOST) {
    for (size_t i = 0; i < len; i++) {
      char c = buf[i];
      if (c == '\n' && (tty->termios.c_oflag & ONLCR)) {
        tty->ops->write(tty, "\r\n", 2);
      } else {
        tty->ops->write(tty, &c, 1);
      }
    }
    return len;
  }

  return tty->ops->write(tty, buf, len);
}
