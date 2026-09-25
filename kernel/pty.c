#include <autoconf.h>
#ifdef CONFIG_TTY

#include <kernel/kmalloc.h>
#include <kernel/string.h>
#include <kernel/tty.h>

static int pty_count = 0;
#define MAX_PTY 16
static struct tty *pty_slaves[MAX_PTY];

static int pty_master_write(struct tty *master, const char *buf, size_t len) {
  struct tty *slave = (struct tty *)master->driver_data;
  if (!slave)
    return len;
  for (size_t i = 0; i < len; i++)
    tty_insert_flip_char(slave, buf[i]);
  return len;
}

static int pty_slave_write(struct tty *slave, const char *buf, size_t len) {
  struct tty *master = (struct tty *)slave->driver_data;
  if (!master)
    return len;
  for (size_t i = 0; i < len; i++)
    tty_insert_flip_char(master, buf[i]);
  return len;
}

static struct tty_ops master_ops = {.write = pty_master_write};
static struct tty_ops slave_ops = {.write = pty_slave_write};

struct tty *pty_get_master(void) {
  if (pty_count >= MAX_PTY)
    return NULL;
  int idx = pty_count++;
  struct tty *master = tty_alloc("ptmx", &master_ops);
  struct tty *slave = tty_alloc("pts", &slave_ops);
  if (!master || !slave) {
    if (master)
      tty_free(master);
    if (slave)
      tty_free(slave);
    return NULL;
  }
  master->driver_data = slave;
  slave->driver_data = master;
  master->index = idx;
  slave->index = idx;
  pty_slaves[idx] = slave;
  return master;
}

struct tty *pty_get_slave(int index) {
  if (index < 0 || index >= pty_count)
    return NULL;
  return pty_slaves[index];
}

#endif /* CONFIG_TTY */
