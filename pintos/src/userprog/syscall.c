#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h" //addition
#include "userprog/process.h" //addition
#include "threads/synch.h" //addition
#include "devices/input.h" //addition
#include "filesys/filesys.h" //addition
#include "filesys/file.h" //addition

static void syscall_handler (struct intr_frame *);
void halt (void);
void exit (int);
tid_t exec (const char*);
int wait (tid_t);
bool create (const char*, unsigned);
bool remove (const char*);
int open (const char*);
int filesize (int);
int read (int, void*, unsigned);
int write (int, const void*, unsigned);
void seek (int, unsigned);
unsigned tell (int);
void close (int);

static int get_user (const uint8_t*);
static bool put_user (uint8_t*, uint8_t);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  //printf ("system call!\n");
  //thread_exit ();

  //hex_dump ((uintptr_t) f->esp, f->esp, (size_t) 160, true);
  //printf ("%p %d\n", f->esp, *(int*)f->esp);

  uint8_t status;
  tid_t pid;
  int fd;
  void* buffer;
  unsigned length;
  char* file;

  switch (get_user ((uint8_t*) f->esp))
  {
    case SYS_HALT:
      //printf ("halt\n");
      shutdown_power_off ();
      break;
    case SYS_EXIT: // exit system call
      //printf ("exit %d\n", *(int*)(f->esp + 4));
      status = get_user ((uint8_t*) f->esp + 4);
      exit (status);
      put_user ((uint8_t*) f->eax, status);
      break;
    case SYS_EXEC:
      //printf ("exec %s\n", *(char**)(f->esp + 4));
      pid = exec ((char*) get_user ((uint8_t*) f->esp + 4));
      put_user ((uint8_t*) f->eax, (uint8_t) pid);
      break;
    case SYS_WAIT:
      //printf ("wait %d\n", *(tid_t*)(f->esp + 4));
      put_user ((uint8_t*) f->eax, (uint8_t) wait ((tid_t) get_user ((uint8_t*) f->esp + 4)));
      break;
    case SYS_CREATE:
      //printf ("create %s %d\n", *(char**)(f->esp + 4), *(unsigned*)(f->esp + 8));
      length = (unsigned) get_user ((uint8_t*) f->esp + 8);
      file = *(char**)(f->esp + 4);
      put_user ((uint8_t*) f->eax, (uint8_t) create (file, length));
      break;
    case SYS_REMOVE:
      //printf ("remove %s\n", *(char**)(f->esp + 4));
      put_user ((uint8_t*) f->eax, (uint8_t) remove ((char*) get_user ((uint8_t*) f->esp + 4)));
      break;
    case SYS_OPEN:
      //printf ("open %s\n", *(char**)(f->esp + 4));
      put_user ((uint8_t*) f->eax, (uint8_t) open ((char*) get_user ((uint8_t*) f->esp + 4)));
      break;
    case SYS_FILESIZE:
      //printf ("filesize %d\n", *(int*)(f->esp + 4));
      put_user ((uint8_t*) f->eax, (uint8_t) filesize ((int) get_user ((uint8_t*) f->esp + 4)));
      break;
    case SYS_READ:
      //printf ("read %d %p %d\n", *(int*)(f->esp + 4), *(void**)(f->esp + 8), *(unsigned*)(f->esp + 12));
      length = (unsigned) get_user ((uint8_t*) f->esp + 12);
      buffer = *(void**)(f->esp + 8);
      fd = *(int*)(f->esp + 4);
      put_user ((uint8_t*) f->eax, (uint8_t) read (fd, buffer, length));
      break;
    case SYS_WRITE: // write system call
      //printf ("write %d %p %d\n", *(int*)(f->esp + 4), *(void**)(f->esp + 8), *(unsigned*)(f->esp + 12));
      length = (unsigned) get_user ((uint8_t*) f->esp + 12);
      buffer = *(void**)(f->esp + 8);
      fd = *(int*)(f->esp + 4);
      put_user ((uint8_t*) f->eax, (uint8_t) write (fd, buffer, length));
      break;
    case SYS_SEEK:
      //printf ("seek %d %d\n", *(int*)(f->esp + 4), *(unsigned*)(f->esp + 8));
      length = (unsigned) get_user ((uint8_t*) f->esp + 8);
      fd = *(int*)(f->esp + 4);
      seek (fd, length);
      break;
    case SYS_TELL:
      //printf ("tell %d\n", *(int*)(f->esp + 4));
      put_user ((uint8_t*) f->eax, (uint8_t) tell ((int) get_user ((uint8_t*) f->esp + 4)));
      break;
    case SYS_CLOSE:
      //printf ("close %d\n", *(int*)(f->esp + 4));
      close ((int) get_user ((uint8_t*) f->esp + 4));
      break;
  }
}

void
exit (int status) //exit system call
{
  thread_current ()->exit_status = status;
  thread_exit ();
}

tid_t
exec (const char *cmd_line)
{
  return process_execute (cmd_line);
}

int
wait (tid_t pid)
{
/*
  for (int i = 0; i < 1000000000; i++);
  return -1;
*/
  return process_wait (pid);
}

bool
create (const char *file, unsigned initial_size)
{
  if (file == NULL)
    return false;
  return filesys_create (file, initial_size);
}

bool
remove (const char *file)
{
  return filesys_remove(file);
}

int
open (const char *file)
{
  unsigned fd;
  fd = filesys_open(file);
  if(fd == -1)
    return -1;
  //3upper goes
}

int
filesize (int fd)
{
  return file_length(fd);
}

int
read (int fd UNUSED, void *buffer UNUSED, unsigned size UNUSED)
{
  unsigned i;
  uint8_t* buffer_ = (uint8_t*) buffer;
  if (fd == 0)
  {
    for (i = 0; i < size; i++)
    {
      buffer_[i] = input_getc ();
      if (buffer_[i] == '\0')
        break;
    }
    return i;
  }
  return -1;
}

int
write (int fd, const void *buffer, unsigned size) //write system call
{
  if (fd == 1)
  {
    putbuf (buffer, size);
    return (int) size;
  }
  return 0;
}

void
seek (int fd, unsigned position)
{
  file_seek(fd, position);
}

unsigned
tell (int fd)
{
  return file_tell(fd);
}

void
close (int fd)
{
  file_close(fd);
}

static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*uaddr));
  return result;
}

static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:" : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}
