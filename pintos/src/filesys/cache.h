#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/off_t.h"
#include "devices/block.h"

void cache_init (void);
void cache_read (block_sector_t, void*);
void cache_read_at (block_sector_t, void*, off_t, off_t);
void cache_write (block_sector_t, const void*);
void cache_write_at (block_sector_t, const void*, off_t, off_t);

void cache_flush (void);

#endif /* filesys/cache.h */
