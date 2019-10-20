#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h" //addition
#include "userprog/process.h" //addition
#include "threads/synch.h" //addition

static void syscall_handler (struct intr_frame *);

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

  int int_;
  char* str_;
  tid_t pid_;
  unsigned unsigned_;
  void* buf_;

  int sysnum = *(int*)(f->esp);

  switch (sysnum)
  {
    case SYS_HALT:
      shutdown_power_off ();
      break;
    case SYS_EXIT:
      int_ = *(int*)(f->esp + 4);
      exit (int_);
      f->eax = (uint32_t) int_;
      break;
    case SYS_EXEC:
      str_ = *(char**)(f->esp + 4);
      f->eax = (uint32_t) exec (str_);
      break;
    case SYS_WAIT:
      pid_ = *(tid_t*)(f->esp + 4);
      f->eax = (uint32_t) wait (pid_);
      break;
    case SYS_CREATE:
      break;
    case SYS_REMOVE:
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      break;
    case SYS_READ:
      break;
    case SYS_WRITE:
      unsigned_ = *(unsigned*)(f->esp + 12);
      buf_ = *(void**)(f->esp + 8);
      int_ = *(int*)(f->esp + 4);
      f->eax = (uint32_t) write (int_, buf_, unsigned_);
      break;
    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
      break;
  }
}

void
exit (int status)
{
  thread_current ()->exit_status = status;
  thread_exit ();
}

tid_t
exec (const char* cmd_line)
{
  tid_t pid = process_execute (cmd_line);
  sema_down (&thread_current ()->exec_sema);
  return thread_current ()->exec_status ? pid : (tid_t) -1;
}

int
wait (tid_t pid)
{
  return process_wait (pid);
}

int
write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf (buffer, size);
    return size;
  }
  return 0;
}













