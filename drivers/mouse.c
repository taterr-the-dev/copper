#include <kernel/types.h>
#include <kernel/io.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/proc.h>
#include <kernel/arch.h>
#include <kernel/syscall.h>

#define MOUSE_BUF_SIZE 512
static uint8_t mouse_buf[MOUSE_BUF_SIZE];
static volatile int mouse_buf_head = 0;
static volatile int mouse_buf_tail = 0;
static volatile int mouse_buf_count = 0;
static struct task *mouse_waiter = NULL;

static void mouse_buf_push(uint8_t byte) {
    if (mouse_buf_count < MOUSE_BUF_SIZE) {
        mouse_buf[mouse_buf_head] = byte;
        mouse_buf_head = (mouse_buf_head + 1) % MOUSE_BUF_SIZE;
        mouse_buf_count++;
    }
}

static void wait_input() { while ((inb(0x64) & 1) == 0); }
static void wait_output() { while (inb(0x64) & 2); }
static void mouse_write(uint8_t data) {
    wait_output(); outb(0x64, 0xD4);
    wait_output(); outb(0x60, data);
    wait_input(); inb(0x60);
}

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];

void mouse_irq(void) {
    uint8_t data = inb(0x60);

    switch (mouse_cycle) {
        case 0:
            if (!(data & 0x08)) return;
            mouse_byte[0] = data;
        mouse_cycle++;
        break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = data;
            mouse_cycle = 0;
            mouse_buf_push(mouse_byte[0]);
            mouse_buf_push(mouse_byte[1]);
            mouse_buf_push(mouse_byte[2]);

            if (mouse_waiter && mouse_waiter->state == T_BLOCKED) {
                mouse_waiter->state = T_READY;
                mouse_waiter = NULL;
            }
            break;
    }
}

int mouse_read(char *buf, int len) {
    int rd = 0;
    while (rd < len) {
        if (mouse_buf_count == 0) {
            arch_cli();
            mouse_waiter = current;
            current->state = T_BLOCKED;
            schedule();
            mouse_waiter = NULL;
            arch_sti();
            if (current && current->sig_pending) {
                return rd > 0 ? rd : -EINTR;
            }
            continue;
        }
        buf[rd++] = mouse_buf[mouse_buf_tail];
        mouse_buf_tail = (mouse_buf_tail + 1) % MOUSE_BUF_SIZE;
        mouse_buf_count--;
    }
    return rd;
}

void mouse_init(void) {
    wait_output(); outb(0x64, 0xA8);
    wait_output(); outb(0x64, 0x20);
    wait_input();
    uint8_t status = inb(0x60);
    status |= 2; status &= ~0x20;
    wait_output(); outb(0x64, 0x60);
    wait_output(); outb(0x60, status);

    mouse_write(0xF6);
    mouse_write(0xF4);

    irq_register(12, mouse_irq);
    pic_clear_mask(12);

    con_puts("[PS2] Mouse initialized on IRQ 12\n");
}
