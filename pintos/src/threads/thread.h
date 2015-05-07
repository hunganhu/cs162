
#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <hash.h>
#include "threads/synch.h"
#include "filesys/directory.h"

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
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define FD_MAX 128                      /**Max files opened concurrently */

/** The process info is a communication area between parent and its child.
    When parent fork a child, the child allocates another page to store
    and append it communication info to parent's child_list. When a child
    stops, it breaks the link but keep its status in the area. It lefts 
    for parent to free the resource.
*/
struct process
{
  struct semaphore sema_wait;         /**Event indicator of wait syscall */
  bool    is_exited;                  /**has called exit() */
  int     exit_code;                  /**Exit status number*/ 
  bool    is_waited;                  /**Is waited by parent */
  bool    is_loaded;                  /**Program loaded */
  tid_t   pid;                        /**Process ID */
  struct list_elem child_elem;
  struct semaphore sema_disk;         /**indicator of access filesys */
};

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
                                        /** thread's current priority which */
                                        /** may be derived from donation    */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    int priority_old;                   /** thread original priority,     */
					/** restored after releasing lock */
    struct list all_locks;              /** List of locks hold by this thread */
    int64_t sleep_ticks;                /** time to sleep in ticks */
    int8_t nice;                        /** nice value, initial to 0 */
    int64_t recent_cpu;                 /** total ticks running in fixed-point
					    format, initial to 0 */
    struct dir *cur_dir;                /** working directory */
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    struct file *fd_table[FD_MAX];      /**File descriptor array */
    int     next_fd;                    /**Next file descriptor id */
    tid_t   parent_id;                  /**Parent tid */
    struct file *executable;            /**executable file of this thread. 
					   The file is remained open to 
					   deny write while executing. It is
					   closed when process exits.*/
    struct list child_list;             /**List of children  */
    struct semaphore sema_load;         /**Event indicator of child loaded */
    struct process *process;            /**process info used to communicate 
					   with parent */
#endif
#ifdef VM
    void   *stack_pointer;              /*pointer to the bottom of stack*/
    struct hash supplemental_pages;     /*supplemental hash pages*/
    struct list mmap_list;              /*mmap file list*/
#endif
    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

#ifdef USERPROG
/** Lock used for syscall synchronization */
struct lock syscall_lock;
/** Lock used for accessing file system */
struct lock filesys_lock;
#endif

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);
void thread_sleep_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_set_load_avg (void);        /** recalculate load average */
void append_lock_list (struct lock *);  /** Append the lock to lock_list */
void remove_lock_list (struct lock *);  /** Remove the lock to lock_list */

struct thread * get_thread (tid_t tid); /** Get thread entry from tid */
void thread_set_root_dir (void);        /** set working dir of kernel to root */

/** Reset the priority of a lock holder from the donation priority, the max
    priority of the threads waiting for the lock.*/
void reset_donate_priority (void);

/* Define a list_less_func which return true if a < b, else false. */
bool less_priority (const struct list_elem *,
		    const struct list_elem *,
		    void *);
/* Declare a function type to get the thread priority of a list element */
typedef int (*get_priority_func) (const struct list_elem *);

/* get the thread priority in a semaphore wait list */
int sema_waiter_priority (const struct list_elem *);

/* get the thread priority in a condition wait list */
int cond_waiter_priority (const struct list_elem *);

#endif /* threads/thread.h */
