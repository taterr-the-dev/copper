#include <autoconf.h>
#include <kernel/string.h>
#include <kernel/kmalloc.h>
#include <kernel/types.h>

#define BCACHE_SIZE 256
#define BLOCK_SIZE 4096

struct bcache_entry {
    int valid;
    uint64_t disk_block;
    uint8_t data[BLOCK_SIZE];
    uint32_t last_used;
};

static struct bcache_entry *cache = NULL;
static uint32_t tick = 0;

void bcache_init(void) {
    cache = kmalloc(sizeof(struct bcache_entry) * BCACHE_SIZE);
    if (cache) {
        memset(cache, 0, sizeof(struct bcache_entry) * BCACHE_SIZE);
    }
}

int bcache_read(uint64_t disk_block, void *buf) {
    if (!cache) return 0;
    tick++;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].disk_block == disk_block) {
            memcpy(buf, cache[i].data, BLOCK_SIZE);
            cache[i].last_used = tick;
            return 1;
        }
    }
    return 0;
}

void bcache_store(uint64_t disk_block, const void *buf) {
    if (!cache) return;
    tick++;

    int best = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < BCACHE_SIZE; i++) {
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
        cache[best].disk_block = disk_block;
        memcpy(cache[best].data, buf, BLOCK_SIZE);
        cache[best].last_used = tick;
    }
}

void bcache_invalidate(uint64_t disk_block) {
    if (!cache) return;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].disk_block == disk_block) {
            cache[i].valid = 0;
            break;
        }
    }
}
