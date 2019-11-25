#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "lib/string.h"
#include "lib/random.h" //temp

struct fte
{
  void* kpage;
  tid_t pid;
  void* upage;
  bool present;
};

struct fte* ft;

void
ft_init (void)
{
  ft = malloc (bitmap_size (get_user_pool_bitmap ()) * sizeof (struct fte));
  for (size_t i = 0; i < bitmap_size (get_user_pool_bitmap ()); i++)
  {
    ft[i].present = false;
  }
}

void
ft_set (void* kpage, void* upage)
{
  ASSERT (pg_ofs (kpage) == 0 && pg_ofs (upage) == 0);
  ASSERT (is_kernel_vaddr (kpage) && is_user_vaddr (upage));
  struct fte* fte = ft + (kpage - (void*) get_user_pool_base ()) / PGSIZE;
  fte->pid = thread_tid ();
  fte->upage = upage;
  fte->present = true;
}

void*
ft_get (void* kpage)
{
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));
  struct fte* fte = ft + (kpage - (void*) get_user_pool_base ()) / PGSIZE;
  if (!fte->present) return NULL;
  return fte->upage;
}

void*
ft_remove (void* kpage)
{
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));
  struct fte* fte = ft + (kpage - (void*) get_user_pool_base ()) / PGSIZE;
  if (!fte->present) return NULL;
  fte->present = false;
  return fte->upage;
}

void*
falloc_get_frame (enum palloc_flags flags, size_t kpage_cnt)
{
  struct lock* lock = get_user_pool_lock ();
  struct bitmap* bitmap = get_user_pool_bitmap ();
  uint8_t* base = get_user_pool_base ();
  void* kpages;
  size_t kpage_idx;

  if (kpage_cnt == 0)
    return NULL;

  lock_acquire (lock);
  kpage_idx = bitmap_scan_and_flip (bitmap, 0, kpage_cnt, false);
  lock_release (lock);

  if (kpage_idx != BITMAP_ERROR)
    kpages = base + PGSIZE * kpage_idx;
  else
    kpages = NULL;

  if (is_user_vaddr (kpages)) return NULL;

  if (kpages != NULL)
  {
    if (flags & PAL_ZERO)
      memset (kpages, 0, PGSIZE * kpage_cnt);
  }
  else
  {
    /* eviction */

    void* slots[bitmap_size (bitmap)];
    for (size_t i = 0; i < bitmap_size (bitmap); i++)
    {
      slots[i] = swap_get_slot ();
      if (slots[i] == NULL)
      {
        for (size_t j = 0; j < i; j++)
          swap_free_slot (slots[j]);
        PANIC ("trying eviction but swap full");
      }
    }

    /* choose frame to evict(LRU) -> coded randomly at first */
    size_t victim_start = random_ulong () % (bitmap_size (bitmap) - kpage_cnt);
    struct fte victim;
    void* slot;

    for (size_t i = 0; i < kpage_cnt; i++)
    {
      victim = ft[victim_start + i];
      slot = slots[i];
      swap_write_slot (slot, victim.kpage);
      bool writable = (pde_get_pt (*(thread_from_tid (victim.pid)->pagedir + pd_no (victim.upage))))[pt_no (victim.upage)] & PTE_W;
      spt_set (victim.upage, slot, SPTE_SWAP, writable);
      pagedir_clear_page (thread_from_tid (victim.pid)->pagedir, victim.upage);
    }

    kpages = base + PGSIZE * victim_start;
  }

  return kpages;
}

void
falloc_free_frame (void* kpage, size_t kpage_cnt)
{
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_kernel_vaddr (kpage));
  palloc_free_multiple (kpage, kpage_cnt);
}
