#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "lib/kernel/hash.h"
#include "devices/block.h"

struct ste
{
  struct block* slot;
  struct hash_elem elem;
};

struct hash st;
struct lock st_lock;

unsigned swap_hash_func (const struct hash_elem*, void*);
bool swap_less_func (const struct hash_elem*, const struct hash_elem*, void*);

void
swap_init (void)
{
  hash_init (&st, swap_hash_func, swap_less_func, NULL);
  lock_init (&st_lock);
}

void*
swap_get_slot (void)
{
  struct block* slot = block_get_role (BLOCK_SWAP);
  if (slot == NULL) return NULL;
  struct ste* ste = malloc (sizeof ste);
  ste->slot = slot;

  lock_acquire (&st_lock);
  hash_replace (&st, &ste->elem);
  lock_release (&st_lock);

  return slot;
}

void
swap_free_slot (void* slot)
{
  struct ste* ste1 = malloc (sizeof ste1);
  ste1->slot = slot;

  lock_acquire (&st_lock);
  struct hash_elem* target = hash_find (&st, &ste1->elem);
  struct ste* ste2 = target != NULL ? hash_entry (target, struct ste, elem) : NULL;
  lock_release (&st_lock);

  free (ste1);

  lock_acquire (&st_lock);
  if (target != NULL) hash_delete (&st, &ste2->elem);
  lock_release (&st_lock);

  free (ste2);
}

void
swap_read_slot (void* slot, void* buf)
{
  block_read (slot, PGSIZE, buf);
}

void
swap_write_slot (void* slot, const void* buf)
{
  block_write (slot, PGSIZE, buf);
}

unsigned
swap_hash_func (const struct hash_elem* e, void* aux UNUSED)
{
  return hash_bytes (&hash_entry (e, struct ste, elem)->slot, 4);
}

bool
swap_less_func (const struct hash_elem* a, const struct hash_elem* b, void* aux UNUSED)
{
  struct block* slot_a = hash_entry (a, struct ste, elem)->slot;
  struct block* slot_b = hash_entry (b, struct ste, elem)->slot;
  return slot_a < slot_b;
}
