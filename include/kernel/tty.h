#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <kernel/types.h>

#define NCCS 32
struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_line;
    uint8_t c_cc[NCCS];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
};

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define ECHO (1 << 3)
#define ICANON (1 << 1)
#define ISIG (1 << 0)
#define ICRNL (1 << 8)
#define IXON (1 << 9)
#define OPOST (1 << 0)
#define ONLCR (1 << 2)

#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCSCTTY 0x540E

struct tty;

struct tty_ops {
    int (*write)(struct tty *tty, const char *buf, size_t len);
    void (*close)(struct tty *tty);
};

struct tty {
    int index;
    char name[32];
    struct tty_ops *ops;
    void *driver_data;
    struct termios termios;
    struct winsize winsize;
    int pgrp;
    char read_buf[4096];
    int read_head;
    int read_tail;
    int read_count;
    void *read_waiter;
};

struct tty *tty_alloc(const char *name, struct tty_ops *ops);
void tty_free(struct tty *tty);
void tty_insert_flip_char(struct tty *tty, char ch);
int tty_read(struct tty *tty, char *buf, size_t len);
int tty_write(struct tty *tty, const char *buf, size_t len);
struct tty *pty_get_master(void);
struct tty *pty_get_slave(int index);

#endif
