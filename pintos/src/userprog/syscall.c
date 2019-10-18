#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h" //addition
#include "userprog/process.h" //addition

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
  switch (*(int*)(f->esp))
  {
    case SYS_HALT:
      //printf ("halt\n");
      shutdown_power_off ();
      break;
    case SYS_EXIT: // exit system call
      //printf ("exit %d\n", *(int*)(f->esp + 4));
      exit (*(int*)(f->esp + 4));
      *(int*)(f->eax) = *(int*)(f->esp + 4);
      break;
    case SYS_EXEC:
      //printf ("exec %s\n", *(char**)(f->esp + 4));
      *(tid_t*)(f->eax) = exec (*(char**)(f->esp + 4));
      break;
    case SYS_WAIT:
      //printf ("wait %d\n", *(tid_t*)(f->esp + 4));
      *(int*)(f->eax) = wait (*(tid_t*)(f->esp + 4));
      break;
    case SYS_CREATE:
      //printf ("create %s %d\n", *(char**)(f->esp + 4), *(unsigned*)(f->esp + 8));
      *(bool*)(f->eax) = create (*(char**)(f->esp + 4), *(unsigned*)(f->esp + 8));
      break;
    case SYS_REMOVE:
      //printf ("remove %s\n", *(char**)(f->esp + 4));
      *(bool*)(f->eax) = remove (*(char**)(f->esp + 4));
      break;
    case SYS_OPEN:
      //printf ("open %s\n", *(char**)(f->esp + 4));
      *(int*)(f->eax) = open (*(char**)(f->esp + 4));
      break;
    case SYS_FILESIZE:
      //printf ("filesize %d\n", *(int*)(f->esp + 4));
      *(int*)(f->eax) = filesize (*(int*)(f->esp + 4));
      break;
    case SYS_READ:
      //printf ("read %d %p %d\n", *(int*)(f->esp + 4), *(void**)(f->esp + 8), *(unsigned*)(f->esp + 12));
      *(int*)(f->eax) = read (*(int*)(f->esp + 4), *(void**)(f->esp + 8), *(unsigned*)(f->esp + 12));
      break;
    case SYS_WRITE: // write system call
      //printf ("write %d %p %d\n", *(int*)(f->esp + 4), *(void**)(f->esp + 8), *(unsigned*)(f->esp + 12));
      *(int*)(f->eax) = write (*(int*)(f->esp + 4), *(void**)(f->esp + 8), *(unsigned*)(f->esp + 12));
      break;
    case SYS_SEEK:
      //printf ("seek %d %d\n", *(int*)(f->esp + 4), *(unsigned*)(f->esp + 8));
      seek (*(int*)(f->esp + 4), *(unsigned*)(f->esp + 8));
      break;
    case SYS_TELL:
      //printf ("tell %d\n", *(int*)(f->esp + 4));
      *(unsigned*)(f->eax) = tell (*(int*)(f->esp + 4));
      break;
    case SYS_CLOSE:
      //printf ("close %d\n", *(int*)(f->esp + 4));
      close (*(int*)(f->esp + 4));
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
  tid_t pid = (tid_t) process_execute (cmd_line);
  return pid;
}

int
wait (tid_t pid)
{
  return process_wait (pid);
}

bool
create (const char *file UNUSED, unsigned initial_size UNUSED)
{
  return false;
}

bool
remove (const char *file UNUSED)
{
  return false;
}

int
open (const char *file UNUSED)
{
  return 0;
}

int
filesize (int fd UNUSED)
{
  return 0;
}

int
read (int fd UNUSED, void *buffer UNUSED, unsigned size UNUSED)
{
  return 0;
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
seek (int fd UNUSED, unsigned position UNUSED)
{
}

unsigned
tell (int fd UNUSED)
{
  return (unsigned) 0;
}

void
close (int fd UNUSED)
{
}
