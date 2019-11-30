#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#ifdef VM
void munmap (mapid_t);
#endif

void syscall_init (void);

#endif /* userprog/syscall.h */
