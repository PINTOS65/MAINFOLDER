#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#ifdef VM
#include "threads/thread.h"
void munmap (mapid_t);
#endif

void syscall_init (void);

#endif /* userprog/syscall.h */
