#include "extents.h"
#include <kernel/console.h>

static void ext4_update_extent_header(uint8_t *in, struct ext4_extent_header *eh)
{
    memcpy(in + 40, eh, sizeof(struct ext4_extent_header));
}

uint32_t ext4_extent_lookup(struct ext_sb *s, uint8_t *in, uint32_t lblock)
{
    uint32_t flags;
    memcpy(&flags, in + 32, 4);
    if (!(flags & EXT4_EXTENTS_FL))
        return 0;

    struct ext4_extent_header *eh = (struct ext4_extent_header *)(in + 40);
    if (eh->eh_magic != 0xF30A)
        return 0;

    uint8_t buf[8192];
    uint8_t *cur = in + 40;
    uint16_t depth = eh->eh_depth;

    while (depth > 0) {
        struct ext4_extent_header *hdr = (struct ext4_extent_header *)cur;
        struct ext4_extent_idx *idx = (struct ext4_extent_idx *)(cur + 12);
        uint32_t child_blk = 0;

        for (int i = 0; i < hdr->eh_entries; i++) {
            if (lblock >= idx[i].ei_block) {
                child_blk = idx[i].ei_leaf_lo | ((uint32_t)idx[i].ei_leaf_hi << 16);
            } else {
                break;
            }
        }
        if (!child_blk)
            return 0;

        ext_read_blk(s, child_blk, buf);
        cur = buf;
        depth--;
    }

    struct ext4_extent_header *hdr = (struct ext4_extent_header *)cur;
    struct ext4_extent *ext = (struct ext4_extent *)(cur + 12);

    for (int i = 0; i < hdr->eh_entries; i++) {
        uint32_t start = ext[i].ee_block;
        uint16_t len = ext[i].ee_len & 0x7FFF;
        if (lblock >= start && lblock < start + len) {
            uint32_t phys = ext[i].ee_start_lo | ((uint32_t)ext[i].ee_start_hi << 16);
            return phys + (lblock - start);
        }
    }

    return 0;
}

uint32_t ext4_extent_alloc(struct ext_sb *s, uint8_t *in, uint32_t lblock)
{
    uint32_t flags;
    memcpy(&flags, in + 32, 4);
    if (!(flags & EXT4_EXTENTS_FL))
        return 0;

    struct ext4_extent_header *eh = (struct ext4_extent_header *)(in + 40);
    if (eh->eh_magic != 0xF30A) {
        eh->eh_magic = 0xF30A;
        eh->eh_entries = 0;
        eh->eh_max =
            (s->inode_size - 40 - sizeof(struct ext4_extent_header)) / sizeof(struct ext4_extent);
        eh->eh_depth = 0;
        eh->eh_generation = 0;
    }

    if (eh->eh_depth > 0) {
        con_puts("[EXT4] Extent alloc: Depth > 0 (tree split) not yet supported\n");
        return 0;
    }

    struct ext4_extent *ext = (struct ext4_extent *)(in + 40 + sizeof(struct ext4_extent_header));

    uint32_t pblock = ext_alloc_block(s);
    if (!pblock)
        return 0;

    if (eh->eh_entries > 0) {
        struct ext4_extent *last = &ext[eh->eh_entries - 1];
        uint32_t last_pblock = last->ee_start_lo | ((uint32_t)last->ee_start_hi << 16);
        uint32_t last_len = last->ee_len & 0x7FFF;

        if (last->ee_block + last_len == lblock && last_pblock + last_len == pblock) {
            last->ee_len++;
            ext4_update_extent_header(in, eh);
            return pblock;
        }
    }

    if (eh->eh_entries >= eh->eh_max) {
        con_puts("[EXT4] Extent alloc: Inline extent array full (need tree split)\n");
        return 0;
    }

    struct ext4_extent *new_ext = &ext[eh->eh_entries];
    new_ext->ee_block = lblock;
    new_ext->ee_len = 1;
    new_ext->ee_start_lo = pblock & 0xFFFFFFFF;
    new_ext->ee_start_hi = (pblock >> 16) & 0xFFFF;

    eh->eh_entries++;
    ext4_update_extent_header(in, eh);

    return pblock;
}
