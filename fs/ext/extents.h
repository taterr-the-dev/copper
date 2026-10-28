#ifndef FS_EXT_EXTENTS_H
#define FS_EXT_EXTENTS_H

#include "ext.h"

uint32_t ext4_extent_lookup(struct ext_sb *s, uint8_t *in, uint32_t lblock);
uint32_t ext4_extent_alloc(struct ext_sb *s, uint32_t ino, uint8_t *in, uint32_t lblock);
int ext4_extent_truncate(struct ext_sb *s, uint32_t ino, uint8_t *in, uint32_t keep);
void ext4_extent_init_root(struct ext_sb *s, uint8_t *in);

#endif
