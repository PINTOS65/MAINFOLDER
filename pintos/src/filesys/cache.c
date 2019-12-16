#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "lib/string.h"
#include "devices/timer.h"
#include <stdbool.h>

#define CACHE_SECTOR_CNT 64

struct cte
{
  block_sector_t sector;
  int accessed_cnt;
  int dirty_cnt;
  struct lock lock;
  bool in_use;
};

static void* cache;
static struct cte ct[CACHE_SECTOR_CNT];
static struct lock cache_lock;
static struct condition aheader_cond;
static block_sector_t next_sector;
static int victim_idx;

static int cache_alloc (block_sector_t);
static void cache_flush_thread (void*);
static void cache_aheader_thread (void*) UNUSED;

void
cache_init (void)
{
  cache = palloc_get_multiple (PAL_ASSERT | PAL_ZERO, BLOCK_SECTOR_SIZE * CACHE_SECTOR_CNT / PGSIZE);
  for (int i = 0; i < CACHE_SECTOR_CNT; i++)
  {
    ct[i].in_use = false;
    lock_init (&ct[i].lock);
  }
  lock_init (&cache_lock);
  cond_init (&aheader_cond);
  victim_idx = 0;

  const char* name1 = "cache_flush_thread";
  thread_create (name1, PRI_DEFAULT, cache_flush_thread, NULL);

  //const char* name2 = "cache_aheader_thread";
  //thread_create (name2, PRI_DEFAULT, cache_aheader_thread, NULL);
}

void
cache_read (block_sector_t sector, void* buffer)
{
  cache_read_at (sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void
cache_read_at (block_sector_t sector, void* buffer, off_t size, off_t ofs)
{
  ASSERT (ofs + size <= BLOCK_SECTOR_SIZE);

  lock_acquire (&cache_lock);
  next_sector = sector + 1;
  cond_signal (&aheader_cond, &cache_lock);

  int i;
  for (i = 0; i < CACHE_SECTOR_CNT; i++)
  {
    if (ct[i].in_use && ct[i].sector == sector)
    {
      lock_release (&cache_lock);
      lock_acquire (&ct[i].lock);
      memcpy (buffer, cache + i * BLOCK_SECTOR_SIZE + ofs, size);
      ct[i].accessed_cnt++;
      lock_release (&ct[i].lock);
      break;
    }
  }

  if (i == CACHE_SECTOR_CNT)
  {
    int cache_idx = cache_alloc (sector);
    block_read (fs_device, sector, cache + cache_idx * BLOCK_SECTOR_SIZE);
    memcpy (buffer, cache + cache_idx * BLOCK_SECTOR_SIZE + ofs, size);
    ct[cache_idx].accessed_cnt++;
    lock_release (&ct[cache_idx].lock);
  }
}

void
cache_write (block_sector_t sector, const void* buffer)
{
  cache_write_at (sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void
cache_write_at (block_sector_t sector, const void* buffer, off_t size, off_t ofs)
{
  lock_acquire (&cache_lock);

  int i;
  for (i = 0; i < CACHE_SECTOR_CNT; i++)
  {
    if (ct[i].in_use && ct[i].sector == sector)
    {
      lock_release (&cache_lock);
      lock_acquire (&ct[i].lock);
      memcpy (cache + i * BLOCK_SECTOR_SIZE + ofs, buffer, size);
      ct[i].accessed_cnt++;
      ct[i].dirty_cnt++;
      lock_release (&ct[i].lock);
      break;
    }
  }

  if (i == CACHE_SECTOR_CNT)
  {
    int cache_idx = cache_alloc (sector);
    block_read (fs_device, sector, cache + cache_idx * BLOCK_SECTOR_SIZE);
    memcpy (cache + cache_idx * BLOCK_SECTOR_SIZE + ofs, buffer, size);
    ct[cache_idx].accessed_cnt++;
    ct[cache_idx].dirty_cnt++;
    lock_release (&ct[cache_idx].lock);
  }
}

static int
cache_alloc (block_sector_t sector)
{
  int result;
  int i;
  int min_accessed_cnt = -1;

  for (i = 0; i < CACHE_SECTOR_CNT; i++)
  {
    lock_acquire (&ct[i].lock);
    if (!ct[i].in_use)
    {
      ct[i].sector = sector;
      ct[i].accessed_cnt = 0;
      ct[i].dirty_cnt = 0;
      ct[i].in_use = true;
      result = i;
      lock_release (&cache_lock);
      break;
    }

    if (min_accessed_cnt == -1 || ct[i].accessed_cnt < min_accessed_cnt)
    {
      result = i;
      min_accessed_cnt = ct[i].accessed_cnt;
    }
    else if (ct[i].accessed_cnt == min_accessed_cnt)
      result = (result < victim_idx && victim_idx <= i) ? i : result;

    lock_release (&ct[i].lock);
  }

  /* eviction */
  if (i == CACHE_SECTOR_CNT)
  {
    lock_acquire (&ct[result].lock);
    if (ct[result].dirty_cnt > 0)
      block_write (fs_device, ct[result].sector, cache + result * BLOCK_SECTOR_SIZE);

    ct[result].sector = sector;
    ct[result].accessed_cnt = 0;
    ct[result].dirty_cnt = 0;
    ct[result].in_use = true;
    lock_release (&cache_lock);

    victim_idx++;
  }

  return result;
}

void
cache_flush (void)
{
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SECTOR_CNT; i++)
  {
    lock_acquire (&ct[i].lock);
    if (ct[i].in_use && ct[i].dirty_cnt > 0)
    {
      block_write (fs_device, ct[i].sector, cache + i * BLOCK_SECTOR_SIZE);
      ct[i].dirty_cnt = 0;
    }
    lock_release (&ct[i].lock);
  }
  lock_release (&cache_lock);
}

static void
cache_flush_thread (void* aux UNUSED)
{
  while (true)
  {
    timer_sleep (50);
    cache_flush ();
  }
}

static void
cache_aheader_thread (void* aux UNUSED)
{
  int i;

  while (true)
  {
    lock_acquire (&cache_lock);
    next_sector = 0;
    cond_wait (&aheader_cond, &cache_lock);

    ASSERT (next_sector > 0);

    for (i = 0; i < CACHE_SECTOR_CNT; i++)
    {
      if (ct[i].in_use && ct[i].sector == next_sector)
      {
        lock_release (&cache_lock);
        break;
      }
    }
    if (i == CACHE_SECTOR_CNT)
    {
      int cache_idx = cache_alloc (next_sector);
      block_read (fs_device, next_sector, cache + cache_idx * BLOCK_SECTOR_SIZE);
      lock_release (&ct[cache_idx].lock);
    }
  }
}
