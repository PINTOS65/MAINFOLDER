#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

void swap_init (void);
void* swap_get_slot (void);
void swap_free_slot (void*);
void swap_read_slot (void*, void*);
void swap_write_slot (void*, const void*);

#endif /* vm/swap.h */
