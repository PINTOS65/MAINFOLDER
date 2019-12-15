#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h" //addition

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    bool is_dir;			/* (addition) directory or file? */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = (inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e);

  /* (addition) setting the child's parent */
  if (success && !inode_set_parent (inode_sector, inode_get_inumber (dir->inode)))
  {
    e.in_use = false;
    inode_write_at (dir->inode, &e, sizeof e, ofs);
    success = false;
  }

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  bool locked = false;
  struct lock* lock = inode_dir_lock (dir->inode);
  if (lock != NULL && !lock_held_by_current_thread (lock))
  {
    lock_acquire (lock);
    locked = true;
  }

  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          if (locked) lock_release (lock);
          return true;
        } 
    }
  if (locked) lock_release (lock);
  return false;
}

/* (addition) gets directory entry's name */
void
dir_get_entry_name (struct dir* parent, block_sector_t child, char** name)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (parent != NULL);

  for (ofs = 0; inode_read_at (parent->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
  {
    if (e.in_use && child == e.inode_sector)
    {
      *name = e.name;
      return;
    }
  }
  PANIC ("dir_get_entry_name FAIL: no such entry with the sector %d in dir %d", child, inode_get_inumber (parent->inode));
}

/* (addition) gets directory entry's is_dir */
bool
dir_entry_is_dir (struct dir* parent, block_sector_t child)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (parent != NULL);

  for (ofs = 0; inode_read_at (parent->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
  {
    if (e.in_use && child == e.inode_sector)
    {
      return e.is_dir;
    }
  }
  PANIC ("dir_entry_is_dir FAIL: no such entry with the sector %d in dir %d", child, inode_get_inumber (parent->inode));
  return false;
}

/* (addition) sets directory entry's is_dir = TRUE */
void
dir_entry_set_dir (struct dir* parent, block_sector_t child)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (parent != NULL);

  for (ofs = 0; inode_read_at (parent->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
  {
    if (e.in_use && child == e.inode_sector)
    {
      e.is_dir = true;
      inode_write_at (parent->inode, &e, sizeof e, ofs);
      return;
    }
  }
  PANIC ("dir_entry_set_dir FAIL: no such entry with the sector %d in dir %d", child, inode_get_inumber (parent->inode));
}

/* (addition) sets directory entry's is_dir = FALSE */
void
dir_entry_clear_dir (struct dir* parent, block_sector_t child)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (parent != NULL);

  for (ofs = 0; inode_read_at (parent->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
  {
    if (e.in_use && child == e.inode_sector)
    {
      e.is_dir = false;
      inode_write_at (parent->inode, &e, sizeof e, ofs);
      return;
    }
  }
  PANIC ("dir_entry_clear_dir FAIL: no such entry with the sector %d in dir %d", child, inode_get_inumber (parent->inode));
}