#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include "threads/synch.h" //addition
#include "lib/kernel/bitmap.h" //addition

/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 001,           /* Panic on failure. */
    PAL_ZERO = 002,             /* Zero page contents. */
    PAL_USER = 004              /* User page. */
  };

void palloc_init (size_t user_page_limit);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);

struct lock* get_user_pool_lock (void); //addition
struct bitmap* get_user_pool_bitmap (void); //addition
uint8_t* get_user_pool_base (void); //addition

#endif /* threads/palloc.h */
