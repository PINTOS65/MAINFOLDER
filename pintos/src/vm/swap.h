#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>

void swap_init (void);
bool swap_in (void*, void*);
void* swap_out (const void*);

#endif /* vm/swap.h */
