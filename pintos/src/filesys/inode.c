#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h" //addition
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* addition for indexed files */
#define TOTAL_SECTOR_CNT (BLOCK_SECTOR_SIZE / 4 - 3)
#define DIRECT_SECTOR_CNT 119
#define INDIRECT_SECTOR_CNT 5
#define FILESYS_LIMIT_SECTORS ((1 << 23) / BLOCK_SECTOR_SIZE)
static bool inode_create_indirect (block_sector_t, off_t, size_t* cur_sectorsp);
static bool inode_create_direct (block_sector_t, off_t, size_t* cur_sectorsp);
static void inode_free_all (block_sector_t);

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t sector[TOTAL_SECTOR_CNT];
    //block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    block_sector_t parent;		/* (addition) parent directory sector */
    unsigned magic;                     /* Magic number. */
    //uint32_t unused[125];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* addition for indexed files */
static inline size_t
length_to_total_sectors (const off_t length_)
{
  off_t length = length_;
  size_t result = 1;
  size_t chunk = BLOCK_SECTOR_SIZE;

  int i = 0;
  while (length > 0 && i < DIRECT_SECTOR_CNT)
  {
    result++;
    length -= chunk;
    i++;
  }

  i = 0;
  while (length > 0 && i < DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT)
  {
    result++;
    int j = 0;
    while (length > 0 && j < TOTAL_SECTOR_CNT)
    {
      result++;
      length -= chunk;
      j++;
    }
    i++;
  }

  i = 0;
  while (length > 0 && i < TOTAL_SECTOR_CNT)
  {
    result++;
    int j = 0;
    while (length > 0 && j < TOTAL_SECTOR_CNT)
    {
      result++;
      int k = 0;
      while (length > 0 && k < TOTAL_SECTOR_CNT)
      {
        result++;
        length -= chunk;
        k++;
      }
      j++;
    }
    i++;
  }

  return result;
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock grow_lock;		/* (addition) synch of file growth */
    struct lock* dir_lockp;		/* (addition) synch of dir work */
    struct inode_disk data;             /* Inode content. */
  };

/* addition for file growth */
static bool inode_grow (struct inode*, off_t length);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos >= inode->data.length) return -1;

  block_sector_t sector_idx = pos / BLOCK_SECTOR_SIZE;
  block_sector_t result;
  if (sector_idx < DIRECT_SECTOR_CNT) return inode->data.sector[sector_idx];

  struct inode_disk* disk_inode;
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL) return -1;

  sector_idx -= DIRECT_SECTOR_CNT;
  if (sector_idx < INDIRECT_SECTOR_CNT * TOTAL_SECTOR_CNT)
  {
    cache_read (inode->data.sector[DIRECT_SECTOR_CNT + sector_idx / TOTAL_SECTOR_CNT], disk_inode);
    result = disk_inode->sector[sector_idx % TOTAL_SECTOR_CNT];
  }
  else
  {
    sector_idx -= INDIRECT_SECTOR_CNT * TOTAL_SECTOR_CNT;
    cache_read (inode->data.sector[DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT + 
						sector_idx / (TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT)],
						disk_inode);
    cache_read (disk_inode->sector[(sector_idx / TOTAL_SECTOR_CNT) % TOTAL_SECTOR_CNT], disk_inode);
    result = disk_inode->sector[sector_idx % TOTAL_SECTOR_CNT];
  }

  free (disk_inode);
  return result;

/*
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
*/
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  size_t cur_sectors = 0;

  struct inode_disk *disk_inode = NULL;
  bool success = true;
  //bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */

  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->length = length;
    disk_inode->parent = ROOT_DIR_SECTOR;
    disk_inode->magic = INODE_MAGIC;

    cur_sectors++;
    size_t cur_inodes = 0;
    off_t length_left = length;
    off_t next_length;
    block_sector_t new_sector;
    static char zeros[BLOCK_SECTOR_SIZE];

    while (length_left > 0 && cur_inodes < TOTAL_SECTOR_CNT && cur_sectors < FILESYS_LIMIT_SECTORS)
    {
      if (!free_map_allocate (1, &new_sector))
      {
        success = false;
        break;
      }
      if (cur_inodes < DIRECT_SECTOR_CNT)
      {
        disk_inode->sector[cur_inodes] = new_sector;
        cache_write (new_sector, zeros);
        cur_sectors++;
        cur_inodes++;
        length_left -= length_left < BLOCK_SECTOR_SIZE ? length_left : BLOCK_SECTOR_SIZE;
      }
      else if (cur_inodes < DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT)
      {
        next_length = length_left < TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE ? length_left : TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE;
        if (!inode_create_direct (new_sector, next_length, &cur_sectors))
        {
          free_map_release (new_sector, 1);
          success = false;
          break;
        }
        disk_inode->sector[cur_inodes] = new_sector;
        cur_inodes++;
        length_left -= next_length;
      }
      else
      {
        next_length = length_left < TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE ?
			length_left :
			TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE;
        if (!inode_create_indirect (new_sector, next_length, &cur_sectors))
        {
          free_map_release (new_sector, 1);
          success = false;
          break;
        }
        disk_inode->sector[cur_inodes] = new_sector;
        cur_inodes++;
        length_left -= next_length;
      }
    }

    if (success)
      cache_write (sector, disk_inode);
    free (disk_inode);
  }
  else
    success = false;

/*
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          cache_write (sector, disk_inode);
          //block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                cache_write (disk_inode->start + i, zeros);
                //block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
      free (disk_inode);
    }
*/
  return success;
}

static bool
inode_create_indirect (block_sector_t sector, off_t length, size_t* cur_sectorsp)
{
  struct inode_disk* disk_inode = NULL;
  bool success = true;

  ASSERT (length >= 0);

  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;

    (*cur_sectorsp)++;
    size_t cur_inodes = 0;
    off_t length_left = length;
    off_t next_length;
    block_sector_t new_sector;

    while (length_left > 0 && *cur_sectorsp < FILESYS_LIMIT_SECTORS)
    {
      if (!free_map_allocate (1, &new_sector))
      {
        success = false;
        break;
      }
      next_length = length_left < TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE ? length_left : TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE;
      if (!inode_create_direct (new_sector, next_length, cur_sectorsp))
      {
        free_map_release (new_sector, 1);
        success = false;
        break;
      }
      disk_inode->sector[cur_inodes] = new_sector;
      cur_inodes++;
      length_left -= next_length;
    }

    if (success)
      cache_write (sector, disk_inode);
    free (disk_inode);
  }
  else
    success = false;
  return success;
}

static bool
inode_create_direct (block_sector_t sector, off_t length, size_t* cur_sectorsp)
{
  struct inode_disk* disk_inode = NULL;
  bool success = true;

  ASSERT (length >= 0);

  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    size_t sectors = bytes_to_sectors (length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;

    (*cur_sectorsp)++;
    size_t cur_sectors_data = 0;
    block_sector_t new_sector;
    static char zeros[BLOCK_SECTOR_SIZE];

    while (cur_sectors_data < sectors && *cur_sectorsp < FILESYS_LIMIT_SECTORS)
    {
      if (!free_map_allocate (1, &new_sector))
      {
        success = false;
        break;
      }
      disk_inode->sector[cur_sectors_data] = new_sector;
      cache_write (new_sector, zeros);
      (*cur_sectorsp)++;
      cur_sectors_data++;
    }

    if (success)
      cache_write (sector, disk_inode);
    free (disk_inode);
  }
  else
    success = false;
  return success;
}

static bool
inode_grow (struct inode* inode, off_t length)
{
  ASSERT (length >= 0);

  size_t cur_sectors = length_to_total_sectors (inode->data.length);
  bool success = true;

  off_t length_left = length;
  off_t next_length;
  block_sector_t new_sector;
  static char zeros[BLOCK_SECTOR_SIZE];

  while (length_left > 0 && cur_sectors < FILESYS_LIMIT_SECTORS)
  {
    /* section 1: direct */
    if (inode->data.length < DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE)
    {
      if (inode->data.length % BLOCK_SECTOR_SIZE != 0) //aligning
      {
        next_length = length_left < BLOCK_SECTOR_SIZE - inode->data.length % BLOCK_SECTOR_SIZE ?
			length_left : BLOCK_SECTOR_SIZE - inode->data.length % BLOCK_SECTOR_SIZE;
        inode->data.length += next_length;
        length_left -= next_length;
        continue;
      }

      if (!free_map_allocate (1, &new_sector))
      {
        success = false;
        break;
      }

      inode->data.sector[inode->data.length / BLOCK_SECTOR_SIZE] = new_sector;
      cache_write (new_sector, zeros);
      cur_sectors++;
      next_length = length_left < BLOCK_SECTOR_SIZE ? length_left : BLOCK_SECTOR_SIZE;
      inode->data.length += next_length;
      length_left -= next_length;
    }

    /* section 2: indirect */
    else if (inode->data.length < DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE +
				INDIRECT_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE)
    {
      if ((inode->data.length - DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE) %
			(TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) != 0) //aligning
      {
        block_sector_t sector_idx = (inode->data.length - DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE) /
					(TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) + DIRECT_SECTOR_CNT;
        struct inode_disk* disk_inode = calloc (1, sizeof *disk_inode);
        if (disk_inode == NULL)
        {
          success = false;
          break;
        }
        cache_read (inode->data.sector[sector_idx], disk_inode);

        if (disk_inode->length % BLOCK_SECTOR_SIZE != 0) //2nd-depth aligning
        {
          next_length = length_left < BLOCK_SECTOR_SIZE - disk_inode->length % BLOCK_SECTOR_SIZE ?
			length_left : BLOCK_SECTOR_SIZE - disk_inode->length % BLOCK_SECTOR_SIZE;
          disk_inode->length += next_length;
          inode->data.length += next_length;
          length_left -= next_length;
        }

        while (length_left > 0 &&
		cur_sectors < FILESYS_LIMIT_SECTORS &&
		disk_inode->length < TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE)
        {
          if (!free_map_allocate (1, &new_sector))
          {
            success = false;
            break;
          }

          disk_inode->sector[disk_inode->length / BLOCK_SECTOR_SIZE] = new_sector;
          cache_write (new_sector, zeros);
          cur_sectors++;
          next_length = length_left < BLOCK_SECTOR_SIZE ? length_left : BLOCK_SECTOR_SIZE;
          disk_inode->length += next_length;
          inode->data.length += next_length;
          length_left -= next_length;
        }

        if (success) cache_write (inode->data.sector[sector_idx], disk_inode);
        free (disk_inode);
        if (!success) break;
        continue;
      }

      if (!free_map_allocate (1, &new_sector))
      {
        success = false;
        break;
      }
      inode->data.sector[(inode->data.length - DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE) /
			(TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) + DIRECT_SECTOR_CNT] = new_sector;
      cur_sectors++;
      next_length = length_left < TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE ?
			length_left : TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE;
      if (!inode_create_direct (new_sector, next_length, &cur_sectors))
      {
        free_map_release (new_sector, 1);
        success = false;
        break;
      }
      inode->data.length += next_length;
      length_left -= next_length;
    }

    /* section 3: doubly indirect */
    else
    {
      if ((inode->data.length - DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE -
				INDIRECT_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) %
				(TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) !=
				0) //aligning
      {
        block_sector_t sector_idx = (inode->data.length - DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE -
					INDIRECT_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) /
					(TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) +
					DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT;
        struct inode_disk* disk_inode = calloc (1, sizeof *disk_inode);
        if (disk_inode == NULL)
        {
          success = false;
          break;
        }
        cache_read (inode->data.sector[sector_idx], disk_inode);

        if (disk_inode->length % (TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) != 0) //2nd-depth aligning
        {
          block_sector_t sector_idx2 = disk_inode->length / (TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE);
          struct inode_disk* disk_inode2 = calloc (1, sizeof *disk_inode2);
          if (disk_inode2 == NULL)
          {
            success = false;
            break;
          }
          cache_read (disk_inode->sector[sector_idx2], disk_inode2);

          if (disk_inode2->length % BLOCK_SECTOR_SIZE != 0) //3rd-depth aligning
          {
            next_length = length_left < BLOCK_SECTOR_SIZE - disk_inode2->length % BLOCK_SECTOR_SIZE ?
			length_left : BLOCK_SECTOR_SIZE - disk_inode2->length % BLOCK_SECTOR_SIZE;
            disk_inode2->length += next_length;
            disk_inode->length += next_length;
            inode->data.length += next_length;
            length_left -= next_length;
          }

          while (length_left > 0 &&
		cur_sectors < FILESYS_LIMIT_SECTORS &&
		disk_inode2->length < TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE)
          {
            if (!free_map_allocate (1, &new_sector))
            {
              success = false;
              break;
            }
            disk_inode2->sector[disk_inode2->length / BLOCK_SECTOR_SIZE] = new_sector;
            cache_write (new_sector, zeros);
            cur_sectors++;
            next_length = length_left < BLOCK_SECTOR_SIZE ? length_left : BLOCK_SECTOR_SIZE;
            disk_inode2->length += next_length;
            disk_inode->length += next_length;
            inode->data.length += next_length;
            length_left -= next_length;
          }

          if (success) cache_write (disk_inode->sector[sector_idx2], disk_inode2);
          free (disk_inode2);
          if (!success) break;
          continue;
        }

        while (length_left > 0 &&
		cur_sectors < FILESYS_LIMIT_SECTORS &&
		disk_inode->length < TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE)
        {
          if (!free_map_allocate (1, &new_sector))
          {
            success = false;
            break;
          }
          disk_inode->sector[disk_inode->length / (TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE)] = new_sector;
          cur_sectors++;
          next_length = length_left < TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE ?
			length_left : TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE;
          if (!inode_create_direct (new_sector, next_length, &cur_sectors))
          {
            free_map_release (new_sector, 1);
            success = false;
            break;
          }
          disk_inode->length += next_length;
          inode->data.length += next_length;
          length_left -= next_length;
        }

        if (success) cache_write (inode->data.sector[sector_idx], disk_inode);
        free (disk_inode);
        if (!success) break;
        continue;
      }

      if (!free_map_allocate (1, &new_sector))
      {
        success = false;
        break;
      }
      inode->data.sector[(inode->data.length - DIRECT_SECTOR_CNT * BLOCK_SECTOR_SIZE -
			INDIRECT_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) /
			(TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE) +
			DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT] = new_sector;
      cur_sectors++;
      next_length = length_left < TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE ?
			length_left : TOTAL_SECTOR_CNT * TOTAL_SECTOR_CNT * BLOCK_SECTOR_SIZE;
      if (!inode_create_indirect (new_sector, next_length, &cur_sectors))
      {
        free_map_release (new_sector, 1);
        success = false;
        break;
      }
      inode->data.length += next_length;
      length_left -= next_length;
    }
  }
  if (success) //is this right?
    cache_write (inode->sector, &inode->data);

  return success;
}

static void
inode_free_all (block_sector_t sector)
{
  struct inode_disk* disk_inode1;
  struct inode_disk* disk_inode2;
  struct inode_disk* disk_inode3;

  disk_inode1 = calloc (1, sizeof *disk_inode1);
  disk_inode2 = calloc (1, sizeof *disk_inode2);
  disk_inode3 = calloc (1, sizeof *disk_inode3);
  if (disk_inode1 == NULL || disk_inode2 == NULL || disk_inode3 == NULL) PANIC ("inode free fail because of a calloc fail");

  cache_read (sector, disk_inode1);

  off_t length = disk_inode1->length;
  size_t chunk = BLOCK_SECTOR_SIZE;

  int i = 0;
  while (length > 0 && i < DIRECT_SECTOR_CNT)
  {
    free_map_release (disk_inode1->sector[i], 1);
    length -= chunk;
    i++;
  }

  i = 0;
  while (length > 0 && i < DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT)
  {
    cache_read (disk_inode1->sector[DIRECT_SECTOR_CNT + i], disk_inode2);
    int j = 0;
    while (length > 0 && j < TOTAL_SECTOR_CNT)
    {
      free_map_release (disk_inode2->sector[j], 1);
      length -= chunk;
      j++;
    }
    free_map_release (disk_inode1->sector[DIRECT_SECTOR_CNT + i], 1);
    i++;
  }

  i = 0;
  while (length > 0 && i < TOTAL_SECTOR_CNT)
  {
    cache_read (disk_inode1->sector[DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT + i], disk_inode2);
    int j = 0;
    while (length > 0 && j < TOTAL_SECTOR_CNT)
    {
      cache_read (disk_inode2->sector[j], disk_inode3);
      int k = 0;
      while (length > 0 && k < TOTAL_SECTOR_CNT)
      {
        free_map_release (disk_inode3->sector[k], 1);
        length -= chunk;
        k++;
      }
      free_map_release (disk_inode2->sector[j], 1);
    }
    free_map_release (disk_inode1->sector[DIRECT_SECTOR_CNT + INDIRECT_SECTOR_CNT + i], 1);
    i++;
  }

  free_map_release (sector, 1);

  free (disk_inode1);
  free (disk_inode2);
  free (disk_inode3);
}


/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->grow_lock); //addition
  inode->dir_lockp = malloc (sizeof (struct lock)); //addition
  lock_init (inode->dir_lockp); //addition
  cache_read (inode->sector, &inode->data);
  //block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free (inode->dir_lockp);
          inode_free_all (inode->sector);
          //free_map_release (inode->sector, 1);
          //free_map_release (inode->data.start, bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t *bounce = NULL;

  while (size > 0) 
    {
      lock_acquire (&inode->grow_lock);

      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      lock_release (&inode->grow_lock);

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (sector_idx, buffer + bytes_read);
          //block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          cache_read_at (sector_idx, buffer + bytes_read, chunk_size, sector_ofs);
/*
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
*/
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  //uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* (addition) file growth */
  lock_acquire (&inode->grow_lock);
  if (offset + size > inode->data.length)
  {
    if (!inode_grow (inode, offset + size - inode->data.length))
      PANIC ("FILE GROWTH FAIL: sector %d, length %d, offset %d, size %d", inode->sector, inode->data.length, offset, size);
  }
  lock_release (&inode->grow_lock);

  while (size > 0) 
    {
      lock_acquire (&inode->grow_lock);

      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      lock_release (&inode->grow_lock);

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
          //block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          cache_write_at (sector_idx, buffer + bytes_written, chunk_size, sector_ofs);
          /* We need a bounce buffer. */
/*
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
*/
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
/*
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
*/
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* (addition) getting the parent directory sector */
block_sector_t
inode_get_parent (block_sector_t child)
{
  struct inode_disk* disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL) return -1;

  cache_read (child, disk_inode);
  block_sector_t parent = disk_inode->parent;
  free (disk_inode);
  return parent;
}

/* (addition) setting the parent directory sector */
bool
inode_set_parent (block_sector_t child, block_sector_t parent)
{
  struct inode_disk* disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL) return false;

  cache_read (child, disk_inode);
  disk_inode->parent = parent;
  cache_write (child, disk_inode);
  free (disk_inode);
  return true;
}

/* (addition) whether someone is using this inode */
bool
inode_is_used (struct inode* inode)
{
  return inode->open_cnt > 1;
}

/* (addition) dir_lock to use */
struct lock*
inode_dir_lock (struct inode* inode)
{
  return inode->dir_lockp;
}
