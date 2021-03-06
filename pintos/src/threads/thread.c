#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "filesys/file.h"
#endif
#include "devices/timer.h"

#ifdef VM
#include <user/syscall.h>
#include "vm/frame.h"
#include "vm/page.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of all locks. Locks are added to this list when they are first 
   acquired and removed when they are last released. */
static struct list lock_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
static long load_avg;           /**load average in fixed-point format */
/** multilevel feedback queue  */
static struct list mlfqs_list[PRI_MAX + 1];

static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
static void wake_threads(struct thread *, void * UNUSED);

int thread_active (void);
void thread_refresh_recent_cpu (struct thread *);
void thread_refresh_priority (struct thread *);

#ifdef USERPROG
void init_process(struct thread *);
#endif

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  int i;

  lock_init (&tid_lock);
  if (thread_mlfqs) {
    for (i = PRI_MIN; i <= PRI_MAX; i++)
      list_init (&mlfqs_list[i]);
  } else {
    list_init (&ready_list);
  }
  list_init (&all_list);
  list_init (&lock_list);
#ifdef USERPROG
  lock_init (&syscall_lock);
  lock_init (&filesys_lock);
#endif

  load_avg = 0;   /** initial system wide load average */
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /** initial semaphore in init() to sync program load */
#ifdef USERPROG
  init_process (initial_thread);
#endif
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  int64_t current_ticks = timer_ticks();
  struct list_elem *allelem;    /** element in kernel all_list */  

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /** Check each thread with wake_threads() after each tick. It is assumed that
      interrupts are disabled  because timer_interrupt() is an interrupt
      handler.*/
  thread_foreach(wake_threads, 0);

  if (thread_mlfqs) {
    t->recent_cpu = FADD_I(t->recent_cpu, 1);

    if ((current_ticks % TIMER_FREQ) == 0) {
      thread_set_load_avg ();       /** calculate load average */
      /** calculate priority for each thread, except idle_thread */
      allelem = list_head(&all_list);
      while ((allelem = list_next(allelem)) != list_end(&all_list)) {
	t = list_entry(allelem, struct thread, allelem);
	if (t != idle_thread)
	  thread_refresh_recent_cpu (t);
      }
    }
    if ((current_ticks %TIME_SLICE) == 0) { // update priority for each thread
      allelem = list_head(&all_list);
      while ((allelem = list_next(allelem)) != list_end(&all_list)) {
	t = list_entry(allelem, struct thread, allelem);
	if (t != idle_thread)
	  thread_refresh_priority (t);
      }
    }
    intr_yield_on_return ();
  }
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/**
 * Function for waking up a sleeping thread. It checks
 * whether a thread is being blocked. If TRUE, then
 * check whether the thread's sleep_ticks has reached 0 or not
 * by decrementing it on each conditional statement.
 * If the thread's sleep_ticks has reached 0, then unblock the
 * sleeping thread.
 */
static void
wake_threads(struct thread *t, void *aux UNUSED)
{
  ASSERT (is_thread(t));
  if(t->status == THREAD_BLOCKED) {
    if(t->sleep_ticks > 0) {
      t->sleep_ticks--;
      if(t->sleep_ticks == 0)
	thread_unblock(t);
    }
  }
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
#ifdef USERPROG
  init_process (t);
  t->process->pid = tid;
#endif
  //  if (t != initial_thread)
  //    t->parent_id = running_thread()->tid;

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  /**When a thread is added to the ready list that has a higher priority than
     the currently running thread, the current thread should immediately yield
     and scheduler choose the next thread to run.
   */
  if (t->priority > thread_current ()->priority)
    thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/** We need to disable interrupt before maintaining the ready list.
    Then reset to the original value after done.
 */

void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  if (thread_mlfqs) {
    list_push_back (&mlfqs_list[t->priority], &t->elem); 
  } else {
    list_push_back (&ready_list, &t->elem);
  }
  t->status = THREAD_READY;

  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) { 
    if (thread_mlfqs) {
      list_push_back (&mlfqs_list[cur->priority], &cur->elem); 
    } else {
      list_push_back (&ready_list, &cur->elem);
    }
  }

  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/**Append the lock to lock_list */
void append_lock_list (struct lock *lock)
{
  ASSERT (lock != NULL);
  enum intr_level old_level;

  old_level = intr_disable ();
  list_push_back (&lock_list, &lock->allelem);
  intr_set_level (old_level);
}

/**Remove the lock to lock_list */
void remove_lock_list (struct lock *lock)
{
  ASSERT (lock != NULL);
  enum intr_level old_level;

  old_level = intr_disable ();
  list_remove (&lock->allelem);
  intr_set_level (old_level);
}

/** Reset the priority of a lock holder from the donation priority, the max
    priority of the threads waiting for the lock.
    When a thread's priority is changed, we need to check every lock holder
    whether it should be donated. When we access the shared object lock_list,
    interrupt should be disabled.
*/
void
reset_donate_priority (void)
{
  enum intr_level old_level;
  struct list_elem *allelem;     /** element in kernel lock_list */  
  struct lock *this_lock;
  struct list_elem *max;
  struct thread    *t;

  /** For donate priority, check every lock in kernel lock_list. Update the
      lock holder's priority to be the max priority of the waiting threads.
  */
  old_level = intr_disable ();
  allelem = list_head(&lock_list);
  while ((allelem = list_next(allelem)) != list_end(&lock_list)) {
    this_lock = list_entry(allelem, struct lock, allelem);
    max = list_max(&this_lock->semaphore.waiters, less_priority,
		   sema_waiter_priority);
    t = list_entry(max, struct thread, elem);
    /** reset the holder's priority to the max. donate priority of
	waiting threads if donate priority is higher.
    */
    if (t->priority > this_lock->holder->priority) {
      this_lock->holder->priority = t->priority;
      reset_donate_priority ();
    }
  }
  intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
/**If the current thread no longer has the highest priority, yields.
 */
void
thread_set_priority (int new_priority) 
{
  struct list_elem *max;
  struct thread *t = thread_current ();

  t->priority = new_priority;
  t->priority_old = new_priority;

  if (!thread_mlfqs) {
    /** Recalculate the donate priority for each lock holder */
    reset_donate_priority ();
    
    /** Find the thread with max priority in ready queue. If it higher than 
	current thread, yield.
    */
    max = list_max(&ready_list, less_priority, sema_waiter_priority);
    int max_priority = list_entry(max, struct thread, elem)->priority;
    if (t->priority < max_priority)
      thread_yield ();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  ASSERT (nice >= -20 && nice <= 20);
  struct thread *t = thread_current();
  int max_priority = 0;
  int i;

  t->nice = nice;
  thread_refresh_recent_cpu (t);
  thread_refresh_priority (t);
  for (i = PRI_MIN; i <= PRI_MAX; i++) {
    if (!list_empty(&mlfqs_list[PRI_MAX - i])) {
      max_priority = i;
      break;
    }
  }
  if (t->priority < max_priority)
    thread_yield ();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return TO_INT(load_avg * 100);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  struct thread *t = thread_current();
  return ROUND_TO_INT(t->recent_cpu * 100);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->magic = THREAD_MAGIC;
  t->cur_dir = NULL;         // set working directory
  /** for donate priority */
  t->priority = priority;
  t->priority_old = priority;
  /** for multilevel feedback queue schudule*/
  t->nice = 0;
  t->recent_cpu = 0;

  /** Initial lock list */
  list_init (&t->all_locks);

#ifdef VM
  list_init(&t->mmap_list);
#endif
  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
/**For priority scheduling, chooses and returns the thread with
   the highest priority to be scheduled. 
*/
static struct thread *
next_thread_to_run (void) 
{
  enum intr_level old_level;
  int i;
  bool all_empty = true;
  struct list_elem *max;

  if (thread_mlfqs) {
    old_level = intr_disable ();
    for (i = PRI_MIN; i <= PRI_MAX; i++) {
      if (!list_empty(&mlfqs_list[PRI_MAX - i])) {
	max = list_pop_front (&mlfqs_list[PRI_MAX - i]);
	all_empty = false;
	break;
      }
    }
    intr_set_level (old_level);
    if (all_empty)
      return idle_thread;
    else
      return list_entry (max, struct thread, elem);
  } else {
    if (list_empty (&ready_list))
      return idle_thread;
    else {
      old_level = intr_disable ();
      struct list_elem *max = list_max(&ready_list, less_priority, 
				       sema_waiter_priority);
      list_remove (max);
      intr_set_level (old_level);
      return list_entry (max, struct thread, elem);
    }
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;
 
  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/** recalculate load average. This function must be called with
    interrupts off. */
void thread_set_load_avg () {
  ASSERT (intr_get_level () == INTR_OFF);

  struct list_elem *allelem;    /** element in kernel all_list */  
  struct thread    *t;
  int active = 0;               /**threads are ready, running except idle */

  allelem = list_head(&all_list);
  while ((allelem = list_next(allelem)) != list_end(&all_list)) {
    t = list_entry(allelem, struct thread, allelem);
    if (t != idle_thread && 
	(t->status == THREAD_RUNNING || t->status == THREAD_READY)) {
      active++;
    }
  }

  load_avg = FMUL_F(FDIV_I(TO_FP(59), 60), load_avg) +
             FDIV_I(TO_FP(1), 60) * active;
}
void thread_refresh_recent_cpu (struct thread *t) {
  ASSERT (is_thread(t));
  t->recent_cpu =FADD_I(FMUL_F(FDIV_F((load_avg * 2), FADD_I(load_avg * 2, 1)),
			       t->recent_cpu),
			t->nice);
}
/** recalculate priority for all threads except idle_thread */
void thread_refresh_priority (struct thread *t) {
  ASSERT (is_thread(t));
  enum intr_level old_level;

  int old_priority = t->priority;
  t->priority = PRI_MAX - (t->nice * 2) - TO_INT(t->recent_cpu / 4);
 
  /* recap priority between PRI_MIN and PRI_MAX */
  if (t->priority < PRI_MIN)
    t->priority = PRI_MIN;
  else if (t->priority > PRI_MAX)
    t->priority = PRI_MAX;

  if (t->status == THREAD_READY && t->priority != old_priority) {
    old_level = intr_disable();
    list_remove (&t->elem);
    list_push_back (&mlfqs_list[t->priority], &t->elem);
    intr_set_level (old_level);
  }
}


int thread_active () {
  enum intr_level old_level;
  struct list_elem *allelem;    /** element in kernel lock_list */  
  struct thread    *t;
  int active = 0;               /**threads are ready, running except idle */

  old_level = intr_disable ();
  allelem = list_head(&all_list);
  while ((allelem = list_next(allelem)) != list_end(&all_list)) {
    t = list_entry(allelem, struct thread, allelem);
    if (t != idle_thread && 
	(t->status == THREAD_RUNNING || t->status == THREAD_READY)) {
      active++;
    }
  }
  intr_set_level (old_level);
  return active;
}

/* compare the priority of thread a and the priority of thread b
   return true if a > b
 */
bool less_priority (const struct list_elem *a,
		    const struct list_elem *b,
		    void *aux) {

  get_priority_func get_priority = (get_priority_func) aux;
  return get_priority (a) < get_priority (b);
}

/* get the thread priority in a semaphore wait list */
int sema_waiter_priority (const struct list_elem *a) {
  return list_entry (a, struct thread, elem)->priority;
}

/* get the thread priority in a condition wait list */
int cond_waiter_priority (const struct list_elem *a) {
  return list_entry (list_begin (&(&list_entry(a, struct semaphore_elem, 
					     elem)->semaphore)->waiters),
		     struct thread, elem)->priority;
}

struct thread * get_thread (tid_t tid)
{
  struct thread *t = NULL;
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); 
       e = list_next (e)) {
    t = list_entry(e, struct thread, allelem);
    if (t->tid == tid)
      return t;
  }
  return NULL;
}

void thread_set_root_dir (void)
{
  struct thread *cur = running_thread ();
  cur->cur_dir = dir_open_root ();
}

#ifdef USERPROG
void init_process(struct thread *t)
{
  memset (t->fd_table, 0, sizeof (t->fd_table)); 
  /** Set next FD id, next value after STDIN_FILENO and STDOUT_FILENO */
  t->next_fd = 2;
  list_init (&t->child_list);
  sema_init (&t->sema_load, 0);

  t->process = malloc (sizeof (struct process));
  ASSERT (t->process != NULL);

  t->process->is_exited = false;
  t->process->is_waited = false;
  t->process->is_loaded = false;
  t->process->exit_code = -1;  
  sema_init (&t->process->sema_wait, 0);  //wait for an event to be happened
  sema_init (&t->process->sema_disk, 0);  //wait for an event to be happened

  t->parent_id = running_thread()->tid;
}
#endif

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
