#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include "threads/palloc.h"

void* falloc_get_frame (enum palloc_flags);
void falloc_free_frame (void*);

void ft_init (void);
void ft_set (void*, void*);
void* ft_get (void*);
void* ft_remove (void*);
void ft_pin (void*);
void ft_unpin (void*);

#endif /* vm/frame.h */
