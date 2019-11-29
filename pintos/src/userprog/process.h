#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#ifdef VM
#include "threads/synch.h" //addition
#endif

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#ifdef VM
struct semaphore pf_sema;
#endif

#endif /* userprog/process.h */
