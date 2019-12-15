#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/directory.h" //addition

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
void* filesys_open (const char *name);
void* filesys_open_with_is_dir (const char*, bool*); //addition for is_dir
bool filesys_remove (const char *name);

/* (addition) file path parsing */
bool filesys_parse (const char* name_, struct dir** dir, char* dst, bool* is_dir);

#endif /* filesys/filesys.h */
