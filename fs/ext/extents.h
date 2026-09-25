#ifndef FS_EXT_EXTENTS_H
#define FS_EXT_EXTENTS_H

#include "ext.h"

uint32_t ext4_extent_lookup(struct ext_sb *s, uint8_t *in, uint32_t lblock);
uint32_t ext4_extent_alloc(struct ext_sb *s, uint8_t *in, uint32_t lblock);

#endif
