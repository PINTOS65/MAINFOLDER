#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"
#include "devices/block.h"

#define SLOT_CNT (PGSIZE / BLOCK_SECTOR_SIZE)

static struct bitmap* st;

static struct block* swap_block;
static struct lock swap_lock;

void
swap_init (void)
{
  swap_block = block_get_role (BLOCK_SWAP);
  //printf ("swap_block: %#x, swap_size: %d, slot per page: %d\n", (unsigned) swap_block, block_size (swap_block), SLOT_CNT);
  if (swap_block == NULL) PANIC ("Ah Wae Ssibal");
  st = bitmap_create (block_size (swap_block));
  lock_init (&swap_lock);
}

bool
swap_in (void* slot, void* buf)
{
  lock_acquire (&swap_lock);
  size_t start = (slot - (void*) swap_block) / BLOCK_SECTOR_SIZE;
  //size_t start = (unsigned) slot;
  if (!bitmap_all (st, start, SLOT_CNT))
  {
    lock_release (&swap_lock);
    return false;
  }
  bitmap_set_multiple (st, start, SLOT_CNT, false);
  for (size_t i = 0; i < SLOT_CNT; i++)
    block_read (swap_block, start + i, buf + i * BLOCK_SECTOR_SIZE);
  lock_release (&swap_lock);
  return true;
}

void*
swap_out (const void* buf)
{
  lock_acquire (&swap_lock);
  size_t start = bitmap_scan_and_flip (st, 0, SLOT_CNT, false);
  if (start == BITMAP_ERROR)
  {
    lock_release (&swap_lock);
    return NULL;
  }
  lock_release (&swap_lock);
  for (size_t i = 0; i < SLOT_CNT; i++)
  {
    block_write (swap_block, start + i, buf + i * BLOCK_SECTOR_SIZE);
  }

  return (void*) swap_block + start * BLOCK_SECTOR_SIZE;
  //return (void*) start;
}
