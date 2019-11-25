#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include "threads/thread.h"

enum spte_flag
{
  SPTE_FILE = 0,
  SPTE_SWAP = 1,
  SPTE_ZERO = 2,
  SPTE_INVALID = 99
};

void spt_init (void);
void spt_set (void*, void*, enum spte_flag, bool);
void* spt_get_ref (void*);
enum spte_flag spt_get_flag (void*);
bool spt_get_writable (void*);
void* spt_remove (void*);

void spt_free_process (tid_t);

#endif /* vm/page.h */
