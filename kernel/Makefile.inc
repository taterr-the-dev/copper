CFILES += kernel/main.c \
          kernel/string.c \
          kernel/console.c \
          kernel/panic.c \
          kernel/kmalloc.c \
          kernel/bcache.c \
          kernel/elf.c \
          kernel/binfmt.c \
          kernel/blkdev.c

ifeq ($(CONFIG_TTY),y)
	CFILES += kernel/tty.c kernel/pty.c
endif
