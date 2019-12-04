#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "lib/string.h"
#include "lib/random.h" //temp

struct fte
{
  void* kpage;
  tid_t pid;
  void* upage;
  bool present;
  bool pinned;
};

static struct fte* ft;
static struct lock ft_lock;
static size_t victim_idx;

void
ft_init (void)
{
  ft = malloc (bitmap_size (get_user_pool_bitmap ()) * sizeof (struct fte));
  for (size_t i = 0; i < bitmap_size (get_user_pool_bitmap ()); i++)
  {
    ft[i].present = false;
    ft[i].pinned = false;
  }
  victim_idx = 0;
  lock_init (&ft_lock);
}

void
ft_set (void* kpage, void* upage)
{
  ASSERT (pg_ofs (kpage) == 0 && pg_ofs (upage) == 0);
  ASSERT (is_kernel_vaddr (kpage) && is_user_vaddr (upage));

  lock_acquire (&ft_lock);
  struct fte* fte = &ft[(kpage - (void*) get_user_pool_base ()) / PGSIZE];
  fte->kpage = kpage;
  fte->pid = thread_tid ();
  fte->upage = upage;
  fte->present = true;
  lock_release (&ft_lock);
}

void*
ft_get (void* kpage)
{
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));

  lock_acquire (&ft_lock);
  struct fte* fte = &ft[(kpage - (void*) get_user_pool_base ()) / PGSIZE];
  void* result = fte->present ? fte->upage : NULL;
  lock_release (&ft_lock);

  return result;
}

void*
ft_remove (void* kpage)
{
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));

  lock_acquire (&ft_lock);
  struct fte* fte = &ft[(kpage - (void*) get_user_pool_base ()) / PGSIZE];
  void* result = fte->present ? fte->upage : NULL;
  fte->present = false;
  lock_release (&ft_lock);

  return result;
}

void
ft_pin (void* kpage)
{
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));

  lock_acquire (&ft_lock);
  struct fte* fte = &ft[(kpage - (void*) get_user_pool_base ()) / PGSIZE];
  fte->pinned = true;
  lock_release (&ft_lock);
}

void
ft_unpin (void* kpage)
{
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));

  lock_acquire (&ft_lock);
  struct fte* fte = &ft[(kpage - (void*) get_user_pool_base ()) / PGSIZE];
  fte->pinned = false;
  lock_release (&ft_lock);
}

void*
falloc_get_frame (enum palloc_flags flags)
{
  struct lock* lock = get_user_pool_lock ();
  struct bitmap* bitmap = get_user_pool_bitmap ();
  uint8_t* base = get_user_pool_base ();
  void* kpage;
  size_t kpage_idx;

  lock_acquire (lock);
  kpage_idx = bitmap_scan_and_flip (bitmap, 0, 1, false);
  lock_release (lock);

  if (kpage_idx != BITMAP_ERROR)
    kpage = base + PGSIZE * kpage_idx;
  else
    kpage = NULL;

  ASSERT (kpage == NULL || is_kernel_vaddr (kpage));

  lock_acquire (&ft_lock);
  if (kpage != NULL)
  {
    if (flags & PAL_ZERO)
      memset (kpage, 0, PGSIZE);
  }
  else
  {
    /* eviction */

    struct fte victim = ft[victim_idx];

    lock_acquire (lock);
    size_t cycle = bitmap_size (bitmap);
    lock_release (lock);

    size_t i;
    for (i = 0; i < 2 * cycle; i++)
    {
      victim = ft[victim_idx];
      if (pagedir_is_accessed (thread_from_tid (victim.pid)->pagedir, victim.upage))
        pagedir_set_accessed (thread_from_tid (victim.pid)->pagedir, victim.upage, false);
      else if (!victim.pinned)
        break;

      if (victim_idx == cycle - 1)
        victim_idx = 0;
      else victim_idx++;
    }
    if (i == 2 * cycle)
    {
      lock_release (&ft_lock);
      return NULL;
    }

/*
    victim_idx = random_ulong () % (bitmap_size (bitmap) - 1);
    while (ft[victim_idx].pinned)
      victim_idx = random_ulong () % (bitmap_size (bitmap) - 1);
*/

    enum spte_flag flag = spt_get_flag_kernel (victim.pid, victim.upage);
    bool writable = spt_get_writable_kernel (victim.pid, victim.upage);
    void* slot;
    switch (flag)
    {
      case SPTE_MMRY:
        slot = swap_out (victim.kpage);
        if (slot == NULL)
          PANIC ("trying to swap out but swap disk is full");
        spt_set_kernel (victim.pid, victim.upage, slot, SPTE_SWAP, writable);
        break;
      case SPTE_FILE:
      case SPTE_ZERO:
        if (pagedir_is_dirty (thread_from_tid (victim.pid)->pagedir, victim.upage))
        {
          slot = swap_out (victim.kpage);
          if (slot == NULL)
            PANIC ("trying to swap out but swap disk is full");
          spt_set_kernel (victim.pid, victim.upage, slot, SPTE_SWAP, writable);
        }
        break;
      case SPTE_SWAP:
        //printf ("##########PID %d upage %#x kpage %#x################\n", victim.pid, (unsigned) victim.upage, (unsigned) victim.kpage);
        PANIC ("why are you in the swap?");
        break;
      case SPTE_INVALID:
        PANIC ("why are you invalid?");
        break;
    }
    pagedir_clear_page (thread_from_tid (victim.pid)->pagedir, victim.upage);
    //printf ("eviction pid %d upage %#x kpage %#x flag %d writable %d\n", victim.pid, (unsigned) victim.upage, (unsigned) victim.kpage, flag, writable);
    kpage = base + PGSIZE * victim_idx;
  }

  lock_release (&ft_lock);

  return kpage;
}

void
falloc_free_frame (void* kpage)
{
  //printf ("freeing %#x\n", (unsigned) kpage);
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));
  lock_acquire (&ft_lock);
  palloc_free_page (kpage);
  lock_release (&ft_lock);
}
