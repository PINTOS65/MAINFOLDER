#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
//#include "threads/thread.h" //included in the header already
#include "devices/shutdown.h" //addition
#include "userprog/process.h" //addition
#include "threads/synch.h" //addition
#include "filesys/file.h" //addition
#include "filesys/filesys.h" //addition
#include "devices/input.h" //addition
#include "threads/vaddr.h" //addition
#include "userprog/pagedir.h" //addition
#include <string.h> //addition
#include "lib/string.h" //addition
#ifdef VM
#include "vm/page.h" //addition
#include "vm/frame.h" //addition
#include "vm/swap.h" //addition
#include "threads/malloc.h" //addition
#endif
#ifdef FILESYS
#include "filesys/free-map.h" //addition
#include "filesys/inode.h" //addition
#endif

static void syscall_handler (struct intr_frame *);

static void exit (int);
static tid_t exec (const char*);
static int wait (tid_t);
static bool create (const char*, unsigned);
static bool remove (const char*);
static int open (const char*);
static int filesize (int);
static int read (int, void*, unsigned);
static int write (int, const void*, unsigned);
static void seek (int, unsigned);
static unsigned tell (int);
static void close (int);
#ifdef VM
static mapid_t mmap (int, void*);
//static void munmap (mapid_t); //declared in the header already
#endif
#ifdef FILESYS
static bool chdir (const char*);
static bool mkdir (const char*);
static bool readdir (int, char*);
static bool isdir (int);
static int inumber (int);
#endif

//static struct semaphore filesynch;
static void* valid (void*);
static void* valid_buf (void*, unsigned);
static void* valid_str (char*);
#ifdef VM
static void unpin (void*);
static void unpin_buf (void*, unsigned);
static void unpin_str (char*);
static void* esp;
#endif

void
syscall_init (void) 
{
  //sema_init (&filesynch, 1);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  //printf ("system call!\n");
  //thread_exit ();

  int int_;
  char* str_;
  tid_t pid_;
  unsigned unsigned_ = 0;
  void* buf_;

  int sysnum = *(int*) valid (f->esp);

#ifdef VM
  esp = f->esp;
#endif

  switch (sysnum)
  {
    case SYS_HALT:
      shutdown_power_off ();
      break;
    case SYS_EXIT:
      int_ = *(int*) valid (f->esp + 4);
      exit (int_);
      f->eax = (uint32_t) int_;
      break;
    case SYS_EXEC:
      str_ = valid_str (*(char**) valid (f->esp + 4));
      f->eax = (uint32_t) exec (str_);
      break;
    case SYS_WAIT:
      pid_ = *(tid_t*) valid (f->esp + 4);
      f->eax = (uint32_t) wait (pid_);
      break;
    case SYS_CREATE:
      unsigned_ = *(unsigned*) valid (f->esp + 8);
      str_ = valid_str (*(char**) valid (f->esp + 4));
      f->eax = (uint32_t) create (str_, unsigned_);
      break;
    case SYS_REMOVE:
      str_ = valid_str (*(char**) valid (f->esp + 4));
      f->eax = (uint32_t) remove (str_);
      break;
    case SYS_OPEN:
      str_ = valid_str (*(char**) valid (f->esp + 4));
      f->eax = (uint32_t) open (str_);
      break;
    case SYS_FILESIZE:
      int_ = *(int*) valid (f->esp + 4);
      f->eax = (uint32_t) filesize (int_);
      break;
    case SYS_READ:
      unsigned_ = *(unsigned*) valid (f->esp + 12);
      buf_ = valid_buf (*(void**) valid (f->esp + 8), unsigned_);
      int_ = *(int*) valid (f->esp + 4);
      f->eax = (uint32_t) read (int_, buf_, unsigned_);
      break;
    case SYS_WRITE:
      unsigned_ = *(unsigned*) valid (f->esp + 12);
      buf_ = valid_buf (*(void**) valid (f->esp + 8), unsigned_);
      int_ = *(int*) valid (f->esp + 4);
      f->eax = (uint32_t) write (int_, buf_, unsigned_);
      break;
    case SYS_SEEK:
      unsigned_ = *(unsigned*) valid (f->esp + 8);
      int_ = *(int*) valid (f->esp + 4);
      seek (int_, unsigned_);
      break;
    case SYS_TELL:
      int_ = *(int*) valid (f->esp + 4);
      f->eax = (uint32_t) tell (int_);
      break;
    case SYS_CLOSE:
      int_ = *(int*) valid (f->esp + 4);
      close (int_);
      break;
#ifdef VM
    case SYS_MMAP:
      buf_ = *(void**) valid (f->esp + 8);
      int_ = *(int*) valid (f->esp + 4);
      f->eax = mmap (int_, buf_);
      break;
    case SYS_MUNMAP:
      int_ = *(int*) valid (f->esp + 4);
      munmap (int_);
      break;
#endif
#ifdef FILESYS
    case SYS_CHDIR:
      str_ = valid_str (*(char**) valid (f->esp + 4));
      f->eax = chdir (str_);
      break;
    case SYS_MKDIR:
      str_ = valid_str (*(char**) valid (f->esp + 4));
      f->eax = mkdir (str_);
      break;
    case SYS_READDIR:
      str_ = valid_str (*(char**) valid (f->esp + 8));
      int_ = *(int*) valid (f->esp + 4);
      f->eax = readdir (int_, str_);
      break;
    case SYS_ISDIR:
      int_ = *(int*) valid (f->esp + 4);
      f->eax = isdir (int_);
      break;
    case SYS_INUMBER:
      int_ = *(int*) valid (f->esp + 4);
      f->eax = inumber (int_);
      break;
#endif
  }

#ifdef VM
  switch (sysnum)
  {
    case SYS_HALT:
      break;
    case SYS_EXIT:
      unpin (f->esp + 4);
      break;
    case SYS_EXEC:
      unpin_str (*(char**)(f->esp + 4));
      unpin (f->esp + 4);
      break;
    case SYS_WAIT:
      unpin (f->esp + 4);
      break;
    case SYS_CREATE:
      unpin (f->esp + 8);
      unpin_str (*(char**)(f->esp + 4));
      unpin (f->esp + 4);
      break;
    case SYS_REMOVE:
      unpin_str (*(char**)(f->esp + 4));
      unpin (f->esp + 4);
      break;
    case SYS_OPEN:
      unpin_str (*(char**)(f->esp + 4));
      unpin (f->esp + 4);
      break;
    case SYS_FILESIZE:
      unpin (f->esp + 4);
      break;
    case SYS_READ:
      unpin (f->esp + 12);
      unpin_buf (*(void**)(f->esp + 8), unsigned_);
      unpin (f->esp + 8);
      unpin (f->esp + 4);
      break;
    case SYS_WRITE:
      unpin (f->esp + 12);
      unpin_buf (*(void**)(f->esp + 8), unsigned_);
      unpin (f->esp + 8);
      unpin (f->esp + 4);
      break;
    case SYS_SEEK:
      unpin (f->esp + 8);
      unpin (f->esp + 4);
      break;
    case SYS_TELL:
      unpin (f->esp + 4);
      break;
    case SYS_CLOSE:
      unpin (f->esp + 4);
      break;
    case SYS_MMAP:
      //unpin (*(void**)(f->esp + 8)); //not needed
      unpin (f->esp + 8);
      unpin (f->esp + 4);
      break;
    case SYS_MUNMAP:
      unpin (f->esp + 4);
      break;
#ifdef FILESYS
    case SYS_CHDIR:
      unpin_str (*(char**)(f->esp + 4));
      unpin (f->esp + 4);
      break;
    case SYS_MKDIR:
      unpin_str (*(char**)(f->esp + 4));
      unpin (f->esp + 4);
      break;
    case SYS_READDIR:
      unpin_str (*(char**)(f->esp + 8));
      unpin (f->esp + 8);
      unpin (f->esp + 4);
      break;
    case SYS_ISDIR:
      unpin (f->esp + 4);
      break;
    case SYS_INUMBER:
      unpin (f->esp + 4);
      break;
#endif
  }
  unpin (f->esp);
#endif

}

static void
exit (int status)
{
  thread_current ()->exit_status = status;
  thread_exit ();
}

static tid_t
exec (const char* cmd_line)
{
  tid_t pid = process_execute (cmd_line);
  sema_down (&thread_current ()->exec_sema);
  return thread_current ()->exec_status ? pid : (tid_t) -1;
}

static int
wait (tid_t pid)
{
  return process_wait (pid);
}

static bool
create (const char* file, unsigned initial_size)
{
  if (file == NULL) thread_exit ();
  return filesys_create (file, initial_size);
}

static bool
remove (const char* file)
{
  return filesys_remove (file);
}

static int
open (const char* file)
{
  if (file == NULL)
    return -1;

  //sema_down (&filesynch);
#ifdef FILESYS
  bool is_dir;
  void* fileptr = filesys_open_with_is_dir (file, &is_dir);
#else
  void* fileptr = filesys_open (file);
#endif
  if (fileptr == NULL)
  {
    //sema_up (&filesynch);
    return -1;
  }
#ifdef FILESYS
  int result = thread_push_file (fileptr);
  if (is_dir)
  {
    thread_fd_set_dir (result);
  }
  else
  {
    if (strcmp (thread_name (), file) == 0) file_deny_write (fileptr);
    thread_fd_clear_dir (result);
  }
#else
  if (strcmp (thread_name (), file) == 0) file_deny_write (fileptr);
  int result = thread_push_file (fileptr);
#endif
  //sema_up (&filesynch);

  return result;
}

static int
filesize (int fd)
{
  if (fd < 3 || fd >= MAX_FILE_CNT)
    thread_exit ();
#ifdef FILESYS
  ASSERT (!thread_fd_is_dir (fd));
#endif
  struct file* file = thread_get_file (fd);
  return file_length (file);
}

static int
read (int fd, void* buffer, unsigned size)
{
  if (fd == 0)
  {
    for (unsigned i = 0; i < size; i++)
      input_getc();
    return size;
  }
  else if (fd < 0 || fd == 1 || fd == 2 || fd >= MAX_FILE_CNT)
    thread_exit ();
#ifdef FILESYS
  if (thread_fd_is_dir (fd)) thread_exit ();
#endif
  struct file* file = thread_get_file (fd);
  int result = file_read (file, buffer, (off_t) size);
  return result;
}

static int
write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf (buffer, size);
    return size;
  }
  else if (fd < 1 || fd == 2 || fd >= MAX_FILE_CNT)
    thread_exit ();
#ifdef FILESYS
  if (thread_fd_is_dir (fd)) thread_exit ();
#endif
  struct file* file = thread_get_file (fd);
  int result = file_write (file, buffer, (off_t) size);
  return result;
}

static void
seek (int fd, unsigned position)
{
  if (fd < 3 || fd >= MAX_FILE_CNT)
    thread_exit ();

#ifdef FILESYS
  ASSERT (!thread_fd_is_dir (fd));
#endif
 
  //sema_down (&filesynch);
  struct file* file = thread_get_file (fd);
  file_seek (file, (off_t) position);
  //sema_up (&filesynch);
}

static unsigned
tell (int fd)
{
  if (fd < 3 || fd >= MAX_FILE_CNT)
    thread_exit ();

#ifdef FILESYS
  ASSERT (!thread_fd_is_dir (fd));
#endif
 
  //sema_down (&filesynch);
  struct file* file = thread_get_file (fd);
  unsigned result = file_tell (file);
  //sema_up (&filesynch);

  return result;
}

static void
close (int fd)
{
  if (fd < 3 || fd >= MAX_FILE_CNT)
    thread_exit ();

  //sema_down (&filesynch);
  void* file = thread_remove_file (fd);
  if (file != NULL)
  {
#ifdef FILESYS
    if (thread_fd_is_dir (fd)) dir_close (file);
    else file_close (file);
#else
    file_close (file);
#endif
  }
  //sema_up (&filesynch);
}

#ifdef VM
static mapid_t
mmap (int fd, void* addr)
{
#ifdef FILESYS
  ASSERT (!thread_fd_is_dir (fd));
#endif

  sema_down (&pf_sema);

  struct file* file = (fd <= 2 || fd >= MAX_FILE_CNT) ? NULL : thread_get_file (fd);
  off_t size = (file == NULL) ? 0 : file_length (file);
  if (size == 0 || pg_ofs (addr) != 0 || addr == 0 || is_kernel_vaddr (addr))
  {
    sema_up (&pf_sema);
    return -1;
  }

  bool success = true;
  for (int i = 0; i < size; i = i + PGSIZE)
  {
    if (spt_get_flag (addr + i) != SPTE_INVALID)
      success = false;
  }
  if (!success)
  {
    sema_up (&pf_sema);
    return -1;
  }

  file = file_reopen (file);
  for (int i = 0; i < size; i = i + PGSIZE)
  {
    spt_set (addr + i, file, SPTE_FILE, true);
    spte_file_seek (addr + i, i);
  }
  struct map* map = malloc (sizeof *map);
  map->upage = addr;
  map->fd = fd;
  mapid_t result = thread_push_map (map);

  sema_up (&pf_sema);
  return result;
}

void
munmap (mapid_t mapping)
{
  sema_down (&pf_sema);

  struct map* map = thread_remove_map (mapping);
  if (map == NULL)
  {
    sema_up (&pf_sema);
    return;
  }

  void* addr = map->upage;
  struct file* file = thread_get_file (map->fd);
  off_t size = (file == NULL) ? 0 : file_length (file);
  free (map);
  if (size == 0)
  {
    sema_up (&pf_sema);
    return;
  }

  uint32_t* pd = thread_current ()->pagedir;
  for (int i = 0; i < size; i = i + PGSIZE)
  {
    void* kpage = pagedir_get_page (pd, addr + i);
    if (kpage != NULL)
    {
      if (pagedir_is_dirty (pd, addr + i))
      {
        //sema_down (&filesynch);
        size_t write_bytes = file_write_at (file, kpage, PGSIZE, spte_file_tell (addr + i));
        if (write_bytes == 0) PANIC ("why writing back nothing????");
        //sema_up (&filesynch);
      }
      pagedir_clear_page (pd, addr + i);
      falloc_free_frame (kpage);
      ft_remove (kpage);
    }
    else if (spt_get_flag (addr + i) == SPTE_SWAP)
    {
      void* temp = malloc (PGSIZE);
      swap_in (spt_get_ref (addr + i), temp);

      //sema_down (&filesynch);
      file_write_at (file, temp, PGSIZE, spte_file_tell (addr + i));
      //sema_up (&filesynch);

      free (temp);
    }
    spt_remove (addr + i);
  }

  sema_up (&pf_sema);
}
#endif

#ifdef FILESYS
static bool
chdir (const char* name)
{
  struct inode* inode = NULL;

  struct dir* dir;
  bool dummy;
  char parsed_name[NAME_MAX + 1];

  bool success = (filesys_parse (name, &dir, parsed_name, &dummy) && dir != NULL);

  struct lock* lock = (dir == NULL) ? NULL : inode_dir_lock (dir_get_inode (dir));
  if (lock != NULL) lock_acquire (lock);

  if (success)
  {
    if (strlen (parsed_name) == 0)
      thread_current ()->cur_dir = ROOT_DIR_SECTOR;
    else
    {
      if (!dir_lookup (dir, parsed_name, &inode))
        success = false;
      if (success)
        thread_current ()->cur_dir = inode_get_inumber (inode);
    }
  }

  inode_close (inode);
  dir_close (dir);

  if (lock != NULL) lock_release (lock);
  return success;
}

static bool
mkdir (const char* name)
{
  block_sector_t inode_sector = 0;

  struct dir* dir = NULL;
  bool dummy;
  char parsed_name[NAME_MAX + 1];

  bool success = (filesys_parse (name, &dir, parsed_name, &dummy) &&
			dir != NULL &&
                        strlen (parsed_name) > 0 &&
			free_map_allocate (1, &inode_sector) &&
			inode_create (inode_sector, 0));

  struct lock* lock = (dir == NULL) ? NULL : inode_dir_lock (dir_get_inode (dir));
  if (lock != NULL) lock_acquire (lock);

  success = success && dir_add (dir, parsed_name, inode_sector);

  if (success) dir_entry_set_dir (dir, inode_sector);

  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  if (lock != NULL) lock_release (lock);
  return success;
}

static bool
readdir (int fd, char* name)
{
  if (!thread_fd_is_dir (fd)) return false;
  struct dir* dir = (struct dir*) thread_get_file (fd);
  return dir_readdir (dir, name);
}

static bool
isdir (int fd)
{
  ASSERT (thread_get_file (fd) != NULL);
  return thread_fd_is_dir (fd);
}

static int
inumber (int fd)
{
  ASSERT (thread_get_file (fd) != NULL);
  return thread_fd_is_dir (fd) ?
		inode_get_inumber (dir_get_inode ((struct dir*) thread_get_file (fd))) :
		inode_get_inumber (file_get_inode ((struct file*) thread_get_file (fd)));
}
#endif

static void*
valid (void* addr)
{
  uint32_t* pd = thread_current ()->pagedir;
  if (addr == NULL) exit (-1);
  if (is_kernel_vaddr (addr + 3)) exit (-1);
#ifdef VM
  sema_down (&pf_sema);
  void* paddr1 = pagedir_get_page (pd, addr);
  void* paddr2 = pagedir_get_page (pd, addr + 3);
  if (paddr1 != NULL)
    ft_pin (pg_round_down (paddr1));
  if (paddr2 != NULL)
    ft_pin (pg_round_down (paddr2));
  sema_up (&pf_sema);

  if (addr >= esp - 32 && addr >= PHYS_BASE - (1 << 23))
    thread_current ()->saved_esp = esp;
  void* dummy1 UNUSED = *(void**)addr;
  void* dummy2 UNUSED = *(void**)(addr + 3);
#else
  if (pagedir_get_page (pd, addr + 3) == NULL) exit (-1);
  if (pagedir_get_page (pd, addr) == NULL) exit (-1);
#endif

/*
  if (is_kernel_vaddr (addr) || pagedir_get_page (pd, addr) == NULL) exit (-1);
  if (is_kernel_vaddr (addr + 1) || pagedir_get_page (pd, addr + 1) == NULL) exit (-1);
  if (is_kernel_vaddr (addr + 2) || pagedir_get_page (pd, addr + 2) == NULL) exit (-1);
  if (is_kernel_vaddr (addr + 3) || pagedir_get_page (pd, addr + 3) == NULL) exit (-1);
*/
  return addr;
}

static void*
valid_buf (void* buf, unsigned length)
{
  uint32_t* pd = thread_current ()->pagedir;
  if (buf == NULL) exit (-1);
  if (is_kernel_vaddr (buf + length - 1)) exit (-1);
#ifdef VM
  void* paddr;
  void* dummy UNUSED;
  for (unsigned i = 0; i < length; i++)
  {
    sema_down (&pf_sema);
    paddr = pagedir_get_page (pd, buf + i);
    if (paddr != NULL)
      ft_pin (pg_round_down (paddr));
    sema_up (&pf_sema);

    if (buf + i >= esp - 32 && buf + i >= PHYS_BASE - (1 << 23))
      thread_current ()->saved_esp = esp;
    dummy = *(void**)(buf + i);
  }
#else
  for (unsigned i = 0; i < length; i++)
  {
    if (pagedir_get_page (pd, buf + i) == NULL)
      exit (-1);
  }
#endif

  return buf;
}

static void*
valid_str (char* str)
{
  uint32_t* pd = thread_current ()->pagedir;
  if (str == NULL) exit (-1);
  unsigned i = 0;

#ifdef VM
  void* paddr;
  char dummy;
  while (true)
  {
    if (is_kernel_vaddr (str + i))
      exit (-1);

    sema_down (&pf_sema);
    paddr = pagedir_get_page (pd, str + i);
    if (paddr != NULL)
      ft_pin (pg_round_down (paddr));
    sema_up (&pf_sema);

    if ((void*) str + i >= esp - 32 && (void*) str + i >= PHYS_BASE - (1 << 23))
      thread_current ()->saved_esp = esp;
    dummy = *(str + i);
    if (dummy == '\0')
      break;
    i++;
  }
#else
  while (true)
  {
    if (is_kernel_vaddr (str + i))
      exit (-1);
    if (pagedir_get_page (pd, str + i) == NULL)
      exit (-1);
    if (*(str + i) == '\0')
      break;
    i++;
  }
#endif

  return str;
}

#ifdef VM
static void
unpin (void* addr)
{
  uint32_t* pd = thread_current ()->pagedir;
  void* paddr1 = pagedir_get_page (pd, addr);
  void* paddr2 = pagedir_get_page (pd, addr + 3);
  if (paddr1 != NULL)
    ft_unpin (pg_round_down (paddr1));
  if (paddr2 != NULL)
    ft_unpin (pg_round_down (paddr2));
}

static void
unpin_buf (void* buf, unsigned length)
{
  uint32_t* pd = thread_current ()->pagedir;
  void* paddr;
  for (unsigned i = 0; i < length; i++)
  {
    if ((paddr = pagedir_get_page (pd, buf + i)) != NULL)
      ft_unpin (pg_round_down (paddr));
  }
}

static void
unpin_str (char* str)
{
  return unpin_buf (str, strlen (str));
}
#endif
