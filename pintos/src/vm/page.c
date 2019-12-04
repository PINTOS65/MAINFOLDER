#include "vm/page.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "lib/kernel/hash.h"

struct spte
{
  tid_t pid;
  void* vaddr;
  void* ref;
  off_t saved_file_pos;
  enum spte_flag flag;
  bool writable;
  struct hash_elem elem;
};

static struct hash spt;
static struct hash_iterator spt_iterator;
static struct lock spt_lock;
static unsigned spte_hash_func (const struct hash_elem*, void*);
static bool spte_less_func (const struct hash_elem*, const struct hash_elem*, void*);

void
spt_init (void)
{
  hash_init (&spt, spte_hash_func, spte_less_func, NULL);
  lock_init (&spt_lock);
}

void
spt_set (void* vaddr, void* ref, enum spte_flag flag, bool writable)
{
  spt_set_kernel (thread_tid (), vaddr, ref, flag, writable);
}

void
spt_set_kernel (tid_t pid, void* vaddr, void* ref, enum spte_flag flag, bool writable)
{
  ASSERT (pg_ofs (vaddr) == 0);
  ASSERT (vaddr != NULL);
  ASSERT (flag != SPTE_INVALID);
  struct spte* spte = malloc (sizeof *spte);
  spte->pid = pid;
  spte->vaddr = vaddr;
  spte->ref = ref;
  spte->flag = flag;
  spte->writable = writable;

  lock_acquire (&spt_lock);
  hash_replace (&spt, &spte->elem);
  //printf ("spt_set pid %d upage %#x ref %#x flag %d writable %d\n", spte->pid, (unsigned) vaddr, (unsigned) ref, flag, writable);
  lock_release (&spt_lock);
}

void*
spt_get_ref_kernel (tid_t pid, void* vaddr)
{
  ASSERT (pg_ofs (vaddr) == 0);
  struct spte* spte = malloc (sizeof *spte);
  spte->pid = pid;
  spte->vaddr = vaddr;

  lock_acquire (&spt_lock);
  struct hash_elem* target = hash_find (&spt, &spte->elem);
  void* ref = target != NULL ? hash_entry (target, struct spte, elem)->ref : NULL;
  lock_release (&spt_lock);

  free (spte);
  return ref;
}

void*
spt_get_ref (void* vaddr)
{
  return spt_get_ref_kernel (thread_tid (), vaddr);
}

enum spte_flag
spt_get_flag_kernel (tid_t pid, void* vaddr)
{
  ASSERT (pg_ofs (vaddr) == 0);
  struct spte* spte = malloc (sizeof *spte);
  spte->pid = pid;
  spte->vaddr = vaddr;

  lock_acquire (&spt_lock);
  struct hash_elem* target = hash_find (&spt, &spte->elem);
  enum spte_flag flag = target != NULL ? hash_entry (target, struct spte, elem)->flag : SPTE_INVALID;
  lock_release (&spt_lock);

  free (spte);
  return flag;
}

enum spte_flag
spt_get_flag (void* vaddr)
{
  return spt_get_flag_kernel (thread_tid (), vaddr);
}

bool
spt_get_writable_kernel (tid_t pid, void* vaddr)
{
  ASSERT (pg_ofs (vaddr) == 0);
  struct spte* spte = malloc (sizeof *spte);
  spte->pid = pid;
  spte->vaddr = vaddr;

  lock_acquire (&spt_lock);
  struct hash_elem* target = hash_find (&spt, &spte->elem);
  bool writable = target != NULL ? hash_entry (target, struct spte, elem)->writable : false;
  lock_release (&spt_lock);

  free (spte);
  return writable;
}

bool
spt_get_writable (void* vaddr)
{
  return spt_get_writable_kernel (thread_tid (), vaddr);
}

void*
spt_remove (void* vaddr)
{
  ASSERT (pg_ofs (vaddr) == 0);
  struct spte* spte1 = malloc (sizeof *spte1);
  spte1->pid = thread_tid ();
  spte1->vaddr = vaddr;

  lock_acquire (&spt_lock);
  struct hash_elem* target = hash_find (&spt, &spte1->elem);
  struct spte* spte2 = target != NULL ? hash_entry (target, struct spte, elem) : NULL;
  void* ref = spte2->ref;

  hash_delete (&spt, &spte2->elem);
  free (spte2);
  lock_release (&spt_lock);

  free (spte1);

  return ref;
}

void
spt_free_process (tid_t pid)
{
  struct spte* spte;
  struct spte** entries = (struct spte**) malloc (hash_size (&spt) * 4);
  int entries_cnt = 0;

  lock_acquire (&spt_lock);
  hash_first (&spt_iterator, &spt);
  for (struct hash_elem* e = hash_cur (&spt_iterator); e != NULL; e = hash_next (&spt_iterator))
  {
    spte = hash_entry (e, struct spte, elem);
    if (spte->pid == pid)
    {
      void* dummy = malloc (PGSIZE);
      switch (spte->flag)
      {
        case SPTE_MMRY:
          break;
        case SPTE_FILE:
          break;
        case SPTE_SWAP:
          swap_in (spte->ref, dummy);
          break;
        case SPTE_ZERO:
          break;
        case SPTE_INVALID:
          PANIC ("who set you invalid?");
      }
      free (dummy);
      entries[entries_cnt++] = spte;
    }
  }
  for (int i = 0; i < entries_cnt; i++)
  {
    hash_delete (&spt, &entries[i]->elem);
    free (entries[i]);
  }
  lock_release (&spt_lock);

  free (entries);
}

void
spte_file_seek (void* vaddr, off_t pos)
{
  ASSERT (pg_ofs (vaddr) == 0);
  struct spte* spte = malloc (sizeof *spte);
  spte->pid = thread_tid ();
  spte->vaddr = vaddr;

  lock_acquire (&spt_lock);
  struct hash_elem* target = hash_find (&spt, &spte->elem);
  struct spte* target_spte = target != NULL ? hash_entry (target, struct spte, elem) : NULL;

  ASSERT (target_spte != NULL);
  ASSERT (pos >= 0);
  target_spte->saved_file_pos = pos;
  lock_release (&spt_lock);

  free (spte);
}

off_t
spte_file_tell (void* vaddr)
{
  ASSERT (pg_ofs (vaddr) == 0);
  struct spte* spte = malloc (sizeof *spte);
  spte->pid = thread_tid ();
  spte->vaddr = vaddr;

  lock_acquire (&spt_lock);
  struct hash_elem* target = hash_find (&spt, &spte->elem);
  struct spte* target_spte = target != NULL ? hash_entry (target, struct spte, elem) : NULL;

  ASSERT (target_spte != NULL);
  off_t result = target_spte->saved_file_pos;
  lock_release (&spt_lock);

  free (spte);
  return result;
}

static unsigned
spte_hash_func (const struct hash_elem* e, void* aux UNUSED)
{
  struct spte* spte = hash_entry (e, struct spte, elem);
  return hash_int ((int) spte->vaddr * (int) spte->pid);
}

static bool
spte_less_func (const struct hash_elem* a, const struct hash_elem* b, void* aux UNUSED)
{
  tid_t pid_a = hash_entry (a, struct spte, elem)->pid;
  tid_t pid_b = hash_entry (b, struct spte, elem)->pid;
  void* vaddr_a = hash_entry (a, struct spte, elem)->vaddr;
  void* vaddr_b = hash_entry (b, struct spte, elem)->vaddr;

  if (pid_a != pid_b) return pid_a < pid_b;
  return vaddr_a < vaddr_b;
}
