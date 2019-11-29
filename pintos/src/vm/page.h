#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include "threads/thread.h"
#include "filesys/off_t.h"

enum spte_flag
{
  SPTE_MMRY = 0,
  SPTE_FILE = 1,
  SPTE_SWAP = 2,
  SPTE_ZERO = 3,
  SPTE_INVALID = 99
};

void spt_init (void);
void spt_set (void*, void*, enum spte_flag, bool);
void* spt_get_ref (void*);
void* spt_get_ref_kernel (tid_t, void*);
enum spte_flag spt_get_flag (void*);
enum spte_flag spt_get_flag_kernel (tid_t, void*);
bool spt_get_writable (void*);
bool spt_get_writable_kernel (tid_t, void*);
void* spt_remove (void*);

void spt_free_process (tid_t);
void spte_file_seek (void*, off_t);
off_t spte_file_tell (void*);

#endif /* vm/page.h */
