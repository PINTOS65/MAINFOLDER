#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h" //addition

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;

#define MAX_FILE_CNT 20 //(addition) limit of number of open files

/* (addition) memory mapped files */
#ifdef VM
#define MAX_MAP_CNT 10
typedef int mapid_t;
struct map
{
  void* upage;
  int fd;
};
#endif

#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    int64_t alarm_ticks;		/* (addition) when should I awake it? */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    struct semaphore exec_sema;		/* (addition) semaphore for exec that parent holds */
    struct semaphore wait_sema;		/* (addition) semaphore for wait that child holds */
    struct semaphore exit_sema;		/* (addition) semaphore for exit that child holds */
    struct list child_list;		/* (addition) child list */
    struct list_elem childelem;		/* (addition) list element for child_list */
    struct thread* parent;		/* (addition) parent thread */
    uint32_t *pagedir;			/* Page directory. */
    void** file_list;			/* (addition) file descriptor list */
    int exit_status;			/* (addition) exit status */
    bool exec_status;			/* (addition) whether exec(child) is successful */
#endif

#ifdef VM
    struct map** map_list;		/* (addition) memory mapped files list */
    void* saved_esp;			/* (addition) saved esp */
#endif

#ifdef FILESYS
    bool is_dir_list[MAX_FILE_CNT];	/* (addition) is_dir list */
    uint32_t cur_dir;			/* (addition) current directory */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_sleep (void); //addition for alarm clock
void thread_unblock (struct thread *);
void thread_awake (void); //addition for alarm clock

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* addition (project 2) */
#ifdef USERPROG
int thread_push_file (void*);
void* thread_remove_file (int);
void* thread_get_file (int);
#endif

/* addition (project 3) */
struct thread* thread_from_tid (tid_t);

#ifdef VM
mapid_t thread_push_map (struct map*);
struct map* thread_remove_map (mapid_t);
struct map* thread_get_map (mapid_t);
#endif

/* addition (project 4) */
bool is_sleep_list_empty (void);
struct thread* thread_slept_first (void);
#ifdef FILESYS
bool thread_fd_is_dir (int);
void thread_fd_set_dir (int);
void thread_fd_clear_dir (int);
bool thread_on_dir (uint32_t);
#endif

#endif /* threads/thread.h */
