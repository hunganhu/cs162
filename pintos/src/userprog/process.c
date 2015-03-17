#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"  // added for alloc buffer for argv
#include "threads/thread.h"
#include "threads/vaddr.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool argument_passing (int argc, char **argv, void **esp);
bool in_childlist (struct list *);
struct process * process_child (struct list *, tid_t);
void dumpchildlist (struct thread *);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
/** Extend the function to support argument passing.
 */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  char process_name[16];
  char *s, *t; 
  struct thread *cur = thread_current();

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);

  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  
  /** read the first token as process name */
  for (s = process_name, t = fn_copy; *t != '\0'; s++, t++)
    if (*t != ' ' && *t != '\t')
      *s = *t;
    else 
      break;
  *s = '\0';

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (process_name, PRI_DEFAULT, start_process, fn_copy);

  /**the parent process cannot return from the exec until it knows whether
     the child process successfully loaded its executable. You must use
     appropriate synchronization to ensure this.*/
  sema_down(&cur->sema_load);

  struct process *my_child = process_child(&cur->child_list, tid);
  /* thread_create fork a thread "start_process" which load the program 
     specified in fn_copy. It allocates a tid and returns. A valid tid
     does not mean the child thread is loaded sucessfully. We need to check
     the flag "is_loaded" in child thread info.

     If an invalid tid is returned, we should free the page previously 
     allocated. If the child fails in loading, the allocated page is freed in
     function start_process().
   */
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  if (!my_child->is_loaded)
    tid = TID_ERROR;

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *command_line_)
{
  char *command_line;
  struct intr_frame if_;
  bool success, status;
  struct thread *cur = thread_current();
  struct thread *parent;

  char *token, *save_ptr, **argv; // declare variables for strtok_r()
  int argc, i;
  char *delimiters = " \t";                // space, and tab

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /** get the number of arguments*/
  command_line = malloc (strlen(command_line_) + 1); //length include '\0'
  strlcpy (command_line, command_line_, strlen(command_line_) + 1);
  for (token = strtok_r (command_line, delimiters, &save_ptr), argc = 0;
       token != NULL;
       token = strtok_r (NULL, delimiters, &save_ptr))
    argc++;

  /** get the arguments */
  argv = malloc (argc * sizeof(char *));
  strlcpy (command_line, command_line_, strlen(command_line_) + 1);
  for (token = strtok_r (command_line, delimiters, &save_ptr), i = 0;
       token != NULL;
       token = strtok_r (NULL, delimiters, &save_ptr))
    argv[i++] = token;

  success = load (argv[0], &if_.eip, &if_.esp);

  /** Pass the argument to the top of user address space */
  if (success)
    status = argument_passing(argc, argv, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (command_line_);
  free (argv);
  free (command_line);

  /** signal parent the loading is done */
  parent = get_thread (cur->parent_id);
  if (parent != NULL) {
    cur->process->is_loaded = success;
    list_push_back (&parent->child_list, &cur->process->child_elem);
    sema_up (&parent->sema_load);
  }

  if (!success || !status) {
    cur->process->exit_code = -1;
    thread_exit ();
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}
/**
int x;
void *ptr;

void * pointers can never be directly used to access the memory to which they 
point because the size and type of the location is unknown.
if you write
x = *ptr;
you will get the rather obnoxious message “…warning: dereferencing 'void *' 
pointer” followed by “…error: invalid use of void expression”.
There are two ways to avoid this problem:
1. type casting: You can “tell” the compiler the type of the data that “ptr”
   is pointing to, i.e.:
   x= *(int *)ptr;
   This is telling the compiler to treat “ptr” as a pointer to something of
   type “int”, and then copy the integer contents it is pointing to to x.
2. Define a separate integer pointer and copy “ptr” to it; the other technique
   is to simply define another pointer, i.e.:
   int *intPtr;
   intPtr = ptr;
   x = *intPtr;
*/
static
bool push_address (void **stack_ptr, void *address, void **esp) 
{
  if (((int)*esp - ((int)*stack_ptr - sizeof(void *))) > PGSIZE)
    return false;

  *stack_ptr -= sizeof(void *);   // 4-byte address
  *(void **)*stack_ptr = address;
  return true;
}

/** pass the program argument to the top of user addressing space*/
static
bool argument_passing (int argc, char **argv, void **esp)
{
  int i;
  void **arg_addr;
  void *stack_top;
  int arg_len;
  bool success = true;
  void *start_arg;
  long padding;

  stack_top = *esp;
  arg_addr = malloc (argc * sizeof (char *));
  /**push arguments to *esp in reverse order*/
  for (i = argc - 1; i >= 0; i--) {
    arg_len = strlen (argv[i]);
    stack_top = stack_top - (arg_len + 1);
    arg_addr[i] = stack_top;
    strlcpy ((char *) arg_addr[i], argv[i], arg_len + 1);
  }
  /**pad 0 for word align */
  padding = sizeof(void *) - ((long)*esp - (long)stack_top) % sizeof(void *);
  for (i = 0; i < padding; i++) {
    *(char *)(--stack_top) = '\0';
  }
  /**push the addresses of arguments and a fake return address */
  // push a null pointer sentinel to ensure that argv[argc] is a null pointer
  push_address ((void **) &stack_top, NULL, esp);
  // push address of arguments in reverse order
  for (i = argc - 1; i >= 0; i--) {
    push_address ((void **) &stack_top, (void *) arg_addr[i], esp);
  }
  // keep the start address of argument list
  start_arg = stack_top;
  // push address of argv
  push_address ((void **) &stack_top, start_arg, esp);
  // push argc
  push_address ((void **) &stack_top, (void *) argc, esp);
  //push a fake return address
  push_address ((void **) &stack_top, NULL, esp);

  //dump the content of argument stack
  /* hex_dump ((uintptr_t)stack_top, stack_top, ((int)*esp - (int)stack_top),
     true); */
  // initial stack pointer to new position
  *esp = stack_top;
  free (arg_addr);

  return success;
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  if (child_tid == TID_ERROR) // Invalid tid
    return -1;

  struct thread *cur = thread_current ();
  struct thread *child = get_thread (child_tid);
  struct process *my_child;  
  bool is_alived = true;
  int success = -1;
  bool is_mychild;

  /** Parent  --+-------(Wait)-----------(Wait)----->
                |
      Child     +-----------------(End)-----------(End)

      Check 4 conditions:
      1. Is child_tid is my child?
      2. Has it already been waiting?
      3. Is it exited normally or killed by kernel?
      4. Is it alived ?
  */

  if (child == NULL)  // child process is not alived
    is_alived = false;
  
  my_child = process_child(&cur->child_list, child_tid);
  if (my_child == NULL)
    is_mychild = false;
  else
    is_mychild = true;


  if (is_mychild) { // it is my child, 
    if (!my_child->is_waited) { // is not waited by parent
      if (is_alived) {   // my child is alived
	my_child->is_waited = true;
	sema_down (&my_child->sema_wait);
	success = my_child->exit_code;
	list_remove (&my_child->child_elem);
	free (my_child);
      } else {   // child is dead
	if (my_child->is_exited)  // exit normally
	  success = my_child->exit_code;
	else   // killed by external thread
	  success = -1;
	list_remove (&my_child->child_elem);
	free (my_child);
      }
    } // -1 for having been waited already
  } // -1 for not my child

  return success;
}

bool in_childlist (struct list *list)
{
  struct list_elem *e;
  bool success = false;
  struct list_elem *elem = &(thread_current()->process->child_elem);

  for (e = list_begin (list); e != list_end (list); e = list_next(e))
    if (e == elem) {
      success = true;
      break;
    }
  return success;
}
struct process * process_child (struct list *list, tid_t child_tid)
{
  struct process *child = NULL;
  struct list_elem *e;

  for (e = list_begin (list); 
       e != list_end (list); e = list_next(e)) {
    child = list_entry (e, struct process, child_elem);
    if (child->pid == child_tid)
      return child;
  }
  return NULL;
}
/* dump child_list of thread t */
void dumpchildlist (struct thread *t)
{
  struct list_elem *e;
  struct process *child;
  struct list *list = &t->child_list;
  int i = 0;

  for (e = list_begin (list); 
       e != list_end (list); e = list_next(e)) {
    child = list_entry (e, struct process, child_elem);
    printf ("Thread %s(%d): dumplist #%d: ", t->name, t->tid, i++);
    printf ("is_exited=%d, exit_code=%d, is_waited=%d, is_loaded=%d, pid=%d, ",
	    child->is_exited, child->exit_code, 
	    child->is_waited, child->is_loaded, child->pid);
    printf("sema_load={value=%d, "
	   "waiters={head={prev=0x%08"PRIx32", next=0x%08"PRIx32"}, "
	            "tail={prev=0x%08"PRIx32", next=0x%08"PRIx32"}}, "
	      "child_elem={prev=0x%08"PRIx32", next=0x%08"PRIx32"}}",
	   (int)child->sema_wait.value,
	   (unsigned)child->sema_wait.waiters.head.prev,
	   (unsigned)child->sema_wait.waiters.head.next,
	   (unsigned)child->sema_wait.waiters.tail.prev,
	   (unsigned)child->sema_wait.waiters.tail.next,
	   (unsigned)child->child_elem.prev,
	   (unsigned)child->child_elem.next
	   );
    printf ("\n");
   }
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  struct thread *parent = get_thread(cur->parent_id);
  int i;
 
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
#ifdef VMM
  /** Free supplemental page table */
  hash_destroy (&cur->supplemental_pages, page_destroy);
  /** Free mmap list */
  struct list_elem *le;
  struct list *maplist = &cur->mmap_list;
  struct mmap *mmap;
  while (!list_empty (maplist)) {
    le = list_pop_front (maplist);
    mmap = list_entry (le, struct mmap, map_elem);
    free (mmap);
  }
#endif

  /**Free process info in child list */
  struct list_elem *e;
  struct list *list = &cur->child_list;
  struct process *child;
  while (!list_empty (list)) {
    e = list_pop_front (list);
    child = list_entry (e, struct process, child_elem);
    free (child);
  }

  /** Free all file descriptors */
  for (i = 2; i < cur->next_fd; i++)
    if (cur->fd_table[i] != NULL) {
      file_close(cur->fd_table[i]);
      cur->fd_table[i] = NULL;
    }
  file_close (cur->executable);  // close this executable

  printf ("%s: exit(%d)\n", cur->name, cur->process->exit_code);

  if (parent != NULL) {
    if (cur->process->is_waited) {
      sema_up (&cur->process->sema_wait);
    } 
  } else {
    free (cur->process);
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

#ifdef VM
  hash_init (&t->supplemental_pages, page_hash_value, page_hash_less, NULL);
#endif
  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)    //ELFCLASS32-ELFDATA2LSB-EV_CURRENT
      || ehdr.e_type != 2           //2:executable file
      || ehdr.e_machine != 3        //3:Intel architecture
      || ehdr.e_version != 1        //1:current version
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  /* Deny write to executable. */
  file_deny_write (file);

 done:
  /* We arrive here whether the load is successful or not. */
  if (!success)
    file_close (file);
  else
    t->executable = file;
  return success;
}


/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  off_t file_ofs = ofs;
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
#ifdef VM
      struct page *page_entry = page_alloc(upage, writable);
      if (page_entry == NULL) {
	return false;
      } else {
	page_entry->file = file;
	page_entry->file_ofs = file_ofs;
	page_entry->read_bytes = page_read_bytes;
	page_entry->zero_bytes = page_zero_bytes;
      }
#else
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }
#endif

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      file_ofs += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  bool success = false;

#ifdef VM
  struct page *page_entry = page_alloc(((uint8_t *) PHYS_BASE) - PGSIZE, true);
  if (page_entry != NULL) {
    page_entry->file = NULL;
    page_entry->file_ofs = 0;
    page_entry->read_bytes = 0;
    page_entry->zero_bytes = 0;
    success = true;
    *esp = PHYS_BASE;
  } // else success = false as default
#else
  uint8_t *kpage;
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);

  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
#endif

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
