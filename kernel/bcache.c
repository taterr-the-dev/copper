#include <autoconf.h>
#include <kernel/string.h>
#include <kernel/kmalloc.h>
#include <kernel/types.h>
#include <kernel/spinlock.h>

#define BCACHE_SIZE 256
#define BLOCK_SIZE 4096

struct bcache_entry {
    int valid;
    const void *dev;
    uint64_t disk_block;
    uint32_t len;
    uint8_t data[BLOCK_SIZE];
    uint32_t last_used;
};

static struct bcache_entry *cache = NULL;
static uint32_t tick = 0;
DEFINE_SPINLOCK(bcache_lock);

void bcache_init(void) {
    spinlock_init(&bcache_lock);
    cache = kmalloc(sizeof(struct bcache_entry) * BCACHE_SIZE);
    if (cache) {
        memset(cache, 0, sizeof(struct bcache_entry) * BCACHE_SIZE);
    }
}

int bcache_read(const void *dev, uint64_t disk_block, void *buf, uint32_t len) {
    if (!cache || len == 0 || len > BLOCK_SIZE) return 0;
    int hit = 0;
    uint64_t flags;
    spin_lock_irqsave(&bcache_lock, &flags);
    tick++;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dev == dev && cache[i].disk_block == disk_block && cache[i].len == len) {
            memcpy(buf, cache[i].data, len);
            cache[i].last_used = tick;
            hit = 1;
            break;
        }
    }
    spin_unlock_irqrestore(&bcache_lock, flags);
    return hit;
}

void bcache_store(const void *dev, uint64_t disk_block, const void *buf, uint32_t len) {
    if (!cache || len == 0 || len > BLOCK_SIZE) return;
    uint64_t flags;
    spin_lock_irqsave(&bcache_lock, &flags);
    tick++;

    int best = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dev == dev && cache[i].disk_block == disk_block) {
            best = i;
            break;
        }
    }
    for (int i = 0; best < 0 && i < BCACHE_SIZE; i++) {
        if (!cache[i].valid) {
            best = i;
            break;
        }
        if (cache[i].last_used < oldest) {
            oldest = cache[i].last_used;
            best = i;
        }
    }

    if (best >= 0) {
        cache[best].valid = 1;
        cache[best].dev = dev;
        cache[best].disk_block = disk_block;
        cache[best].len = len;
        memcpy(cache[best].data, buf, len);
        cache[best].last_used = tick;
    }
    spin_unlock_irqrestore(&bcache_lock, flags);
}

void bcache_invalidate(const void *dev, uint64_t disk_block) {
    if (!cache) return;
    uint64_t flags;
    spin_lock_irqsave(&bcache_lock, &flags);
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dev == dev && cache[i].disk_block == disk_block) {
            cache[i].valid = 0;
        }
    }
    spin_unlock_irqrestore(&bcache_lock, flags);
}
