ifeq ($(CONFIG_NET),y)
    CFILES += net/socket.c net/ipv4.c net/tcp.c
endif
