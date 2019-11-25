#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include "threads/palloc.h"

void* falloc_get_frame (enum palloc_flags, size_t);
void falloc_free_frame (void*, size_t);

void ft_init (void);
void ft_set (void*, void*);
void* ft_get (void*);
void* ft_remove (void*);

#endif /* vm/frame.h */
