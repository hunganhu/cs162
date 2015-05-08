#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

static void syscall_handler (struct intr_frame *);

static bool access_ok (const void *, unsigned);
static bool valid_user_fd (int);
static uint32_t read_argument (struct intr_frame *, int);

static void sys_halt (void);
static void sys_exit (int);
static pid_t sys_exec (const char *);
static int sys_wait (pid_t);
static bool sys_create (const char *, unsigned);
static bool sys_remove (const char *);
static int sys_open (const char *);
static int sys_filesize (int);
static int sys_read (int, void *, unsigned);
static int sys_write (int, const void *, unsigned);
static void sys_seek (int, unsigned);
static int sys_tell (int);
static void sys_close (int);
static mapid_t sys_mmap (int, void *);
static void sys_munmap (mapid_t);
static bool sys_chdir (const char *);
static bool sys_mkdir (const char *);
static bool sys_readdir (int, char *);
static bool sys_isdir (int);
static int sys_isnumber (int);
static int get_user (const uint8_t *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/** Synchronization:
  Here's the scenario the Pintos docs may be worried about:
  - User program begins a file write, vectors control to your syscall handler
  - syscall handler acquires a lock on writing to disk
  - During the write, a page fault occurs, control is vectored to the PF handler
  - PF handler needs to evict a page back to disk; tries to acquire a lock on
    writing to disk. This lock has already been acquired though; deadlock.
 */
void 
lock_filesys() 
{
  struct thread *t = thread_current();
  if(lock_held_by_current_thread (&filesys_lock)) {
    sema_up (&t->process->sema_disk); // raise semaphore lock has been hold 
  } else {
    lock_acquire (&filesys_lock);
  }
}

void 
unlock_filesys()
{
  struct thread *t = thread_current();
  //if semaphore is up, do not release lock.
  if (!sema_try_down(&t->process->sema_disk)) {
    lock_release (&filesys_lock);
  }
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_no;
  uint32_t arg0, arg1, arg2, arg3;
  struct thread *t = thread_current();

  arg0 = read_argument(f, 0);
  syscall_no = (int) arg0;
  t->stack_pointer = f->esp;

  switch (syscall_no)
    {
    case SYS_HALT:                   /* 0 Halt the operating system. */
      sys_halt ();
      break;
    case SYS_EXIT:                   /* Terminate this process. */
      arg1 = read_argument(f, 1);
      sys_exit((int) arg1);
      break;
    case SYS_EXEC:                   /* Start another process. */
      arg1 = read_argument(f, 1);
      f->eax = sys_exec((char *) arg1);
      break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
      arg1 = read_argument(f, 1);
      f->eax = sys_wait((pid_t) arg1);
      break;
    case SYS_CREATE:                 /* Create a file. */
      arg1 = read_argument(f, 1);
      arg2 = read_argument(f, 2);
      f->eax = sys_create((char *) arg1, (unsigned) arg2);
      break;
     case SYS_REMOVE:                 /* 5 Delete a file. */
      arg1 = read_argument(f, 1);
      f->eax = sys_remove((char *) arg1);
      break;
    case SYS_OPEN:                   /* Open a file. */
      arg1 = read_argument(f, 1);
      f->eax = sys_open((char *) arg1);
      break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
      arg1 = read_argument(f, 1);
      f->eax = sys_filesize((int) arg1);
      break;
    case SYS_READ:                   /* Read from a file. */
      arg1 = read_argument(f, 1);
      arg2 = read_argument(f, 2);
      arg3 = read_argument(f, 3);
      f->eax = sys_read((int) arg1, (void *) arg2, (unsigned) arg3);
     break;
    case SYS_WRITE:                  /* Write to a file. */
      arg1 = read_argument(f, 1);
      arg2 = read_argument(f, 2);
      arg3 = read_argument(f, 3);
      f->eax = sys_write((int) arg1, (void *) arg2, (unsigned) arg3);
      break;
    case SYS_SEEK:                   /* 10 Change position in a file. */
      arg1 = read_argument(f, 1);
      arg2 = read_argument(f, 2);
      sys_seek((int) arg1, (unsigned) arg2);
      break;
    case SYS_TELL:                   /* Report current position in a file. */
      arg1 = read_argument(f, 1);
      f->eax = sys_tell((int) arg1);
      break;
    case SYS_CLOSE:                  /* Close a file. */
      arg1 = read_argument(f, 1);
      sys_close((int) arg1);
      break;

    /* Project 3 and optionally project 4. */
    case SYS_MMAP:                   /* Map a file into memory. */
      arg1 = read_argument(f, 1);
      arg2 = read_argument(f, 2);
      lock_filesys();
      f->eax = sys_mmap((int) arg1, (void *) arg2);
      unlock_filesys();
      break;
    case SYS_MUNMAP:                 /* Remove a memory mapping. */
      arg1 = read_argument(f, 1);
      lock_filesys();
      sys_munmap((mapid_t) arg1);
      unlock_filesys();
      break;

    /* Project 4 only. */
    case SYS_CHDIR:                  /* 15 Change the current directory. */
      arg1 = read_argument(f, 1);
      lock_filesys();
      f->eax = sys_chdir((char *) arg1);
      unlock_filesys();
      break;
    case SYS_MKDIR:                  /* Create a directory. */
      arg1 = read_argument(f, 1);
      lock_filesys();
      f->eax = sys_mkdir((char *) arg1);
      unlock_filesys();
      break;
    case SYS_READDIR:                /* Reads a directory entry. */
      arg1 = read_argument(f, 1);
      arg2 = read_argument(f, 2);
      lock_filesys();
      f->eax = sys_readdir((int) arg1, (char *) arg2);
      unlock_filesys();
      break;
    case SYS_ISDIR:                  /* Tests if a fd represents a directory. */
      arg1 = read_argument(f, 1);
      lock_filesys();
      f->eax = sys_isdir((int) arg1);
      unlock_filesys();
      break;
    case SYS_INUMBER:                /* 19 Returns the inode number for a fd. */
      arg1 = read_argument(f, 1);
      lock_filesys();
      f->eax = sys_isnumber((int) arg1);
      unlock_filesys();
      break;
    default:
      break;
    }
}

/* Check validity of buffer starting at vaddr, with length of size*/
static bool
access_ok (const void * vaddr, unsigned size)
{
  /* If the address exceeds PHYS_BASE, or address is not mapped,  exit -1 */
  if (!is_user_vaddr (vaddr + size) || !is_user_vaddr (vaddr))
    return false;

  int result = get_user (vaddr);
  if (result == -1)
    return false;

  if (size > 0) {
    result = get_user (vaddr + size);
    if (result == -1)
      return false;
  }
  return true;
}

/* check fd is in valid range [0, 128) */
static bool
valid_user_fd (int fd)
{
  return (fd >= 0) && (fd < 128);
}

/** Read the Nth element, specified by offset and length is a world, from 
    the stack. The address of the stack is specified by f->esp.
 */
static uint32_t
read_argument (struct intr_frame *f, int offset)
{
  /* Check address */
  if (!access_ok (f->esp, offset * sizeof (void *)))
    sys_exit(-1);

  return *(uint32_t *)(f->esp + offset * sizeof (void *));
}


static void sys_halt (void)
{
  shutdown_power_off ();
}

static void sys_exit (int status)
{
  struct thread *t = thread_current ();
  t->process->exit_code = status;
  t->process->is_exited = true;

  thread_exit ();
}

static pid_t sys_exec (const char *file)
{
  /** verify parameters */
  if (file == NULL || !access_ok(file, 0))
    sys_exit(-1);
  
  pid_t pid = -1;
  pid = process_execute(file);

  return pid;
}

static int sys_wait (pid_t pid)
{
  int success = -1;
  if (pid == TID_ERROR)
    sys_exit(-1);

  success = process_wait(pid);
  return success;
}

static bool sys_create (const char *file, unsigned initial_size)
{
  /** verify parameters */
  if (!access_ok(file, 0) || *file == '\0')
    sys_exit(-1);

  bool success = false;
  struct file *file_ptr;
  lock_filesys();
  file_ptr = filesys_open (file);
  if (file_ptr == NULL)
    success = filesys_create (file, initial_size);
  else
    file_close (file_ptr);

  unlock_filesys();
  return success;
}

static bool sys_remove (const char *file)
{
  /** verify parameters */
  if (!access_ok(file, 0))
    sys_exit(-1);

  bool success = false;
  //  struct file *file_ptr;
  //  file_ptr = filesys_open (file);
  lock_filesys();
  success = filesys_remove (file);

  /*  if (file_ptr != NULL) {
    file_close (file_ptr);
    success = filesys_remove (file);
  }
  */
  unlock_filesys();
  return success;

}

static int sys_open (const char *file)
{
  /** verify parameters */
  if (file == NULL || !access_ok(file, 0))
    sys_exit(-1);

  struct thread *t = thread_current ();
 
  /* Get file info*/
  lock_filesys();
  struct file *file_ = filesys_open(file);
  unlock_filesys();
  int fd = -1;

  if (file_ != NULL) {
    fd = t->next_fd++;
    t->fd_table[fd] = file_;
  }
  return fd;
}

static int sys_filesize (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd))
    sys_exit(-1);

  struct thread *t = thread_current ();
  int size = -1;
 
  /* Get file info*/
  struct file *file_ = t->fd_table[fd];
  
  if (file_ != NULL) {
    lock_filesys();
    size = file_length(file_);
    unlock_filesys();
  }
  return size;
}

/** Refer to Pintos reference 4.3.5 accessing user memory
   you must not allow page faults to occur while a device driver accesses
   a user buffer passed to file_read, because you would not be able to
   invoke the driver while handling such faults.

   This modification is for sys_read and sys_write calls.
 */
static int sys_read (int fd, void *buffer, unsigned size)
{
  /** verify parameters */
  if (!access_ok (buffer, size) || !valid_user_fd(fd) || fd == STDOUT_FILENO)
    sys_exit(-1);

  struct thread *t = thread_current ();
  int byte_read = -1;
  unsigned i;
  char *buf = (char *) buffer;  // convert void type pointer to char 

  if (fd == STDIN_FILENO) {
    for ( i = 0; i < size; i ++) {
      *(buf + i) = input_getc();
    }
    byte_read = i;
  } if (fd == STDOUT_FILENO) {
    byte_read = -1;
  }
  else {
    /* Get file info*/
    struct file *file_ = t->fd_table[fd];
    int num_read = 0;
    int page_read_bytes;
    uint32_t bytes_to_read;
    void *upage;

    //    if (file_ != NULL)
    //      byte_read = file_read (file_, buffer, size);
    if (file_ != NULL) {
      upage = pg_round_down(buffer);
      byte_read = 0;
      while (size > 0) {
	bytes_to_read = PGSIZE - pg_ofs (buffer);
	page_read_bytes = size < bytes_to_read ? size : bytes_to_read; 
	page_pin (upage);
	lock_filesys ();
	num_read = file_read (file_, buffer, page_read_bytes);
	unlock_filesys ();
	page_unpin (upage);
	
	size -= page_read_bytes;
	buffer += page_read_bytes;
	upage += PGSIZE;
	byte_read += num_read;
      }
    }

  }
  return byte_read;
}

static int sys_write (int fd, const void *buffer, unsigned size)
{
  /** verify parameters */
  if (!access_ok (buffer, size) || !valid_user_fd(fd) || fd == STDIN_FILENO)
    sys_exit(-1);

  struct thread *t = thread_current ();
  unsigned byte_written = -1;

  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    byte_written = size;
  } if (fd == STDIN_FILENO) {
    byte_written = -1;
  }
  else {
    /* Get file info*/
    struct file *file_ = t->fd_table[fd];
    int num_written = 0;
    int page_write_bytes;
    uint32_t bytes_to_write;
    void *upage;

    if (file_ != NULL && !inode_is_dir(file_get_inode(file_))) {
      upage = pg_round_down(buffer);
      byte_written = 0;
      while (size > 0) {
	bytes_to_write = PGSIZE - pg_ofs (buffer);
	page_write_bytes = size < bytes_to_write ? size : bytes_to_write; 
	page_pin (upage);
	lock_filesys ();
	num_written = file_write (file_, buffer, page_write_bytes);
	unlock_filesys ();
	page_unpin (upage);
	
	size -= page_write_bytes;
	buffer += page_write_bytes;
	upage += PGSIZE;
	byte_written += num_written;
      }
    }
  }
  return byte_written;
}

static void sys_seek (int fd, unsigned position)
{
  /** verify parameters */
  if (!valid_user_fd(fd))
    sys_exit(-1);
  struct thread *t = thread_current ();
 
  /* Get file info*/
  struct file *file_ = t->fd_table[fd];
  
  if (file_ != NULL) {
    lock_filesys();
    file_seek(file_, position);
    unlock_filesys();
  }
}

static int sys_tell (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd))
    sys_exit(-1);

  struct thread *t = thread_current ();
  int position = -1;

  /* Get file info*/
  struct file *file_ = t->fd_table[fd];
  
  if (file_ != NULL) {
    lock_filesys();
    position = file_tell(file_);
    unlock_filesys();
  }
  return position;
}

static void sys_close (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd))
    sys_exit(-1);

  struct thread *t = thread_current ();

  /* Get file info*/
  struct file *file_ = t->fd_table[fd];
  
  if (file_ != NULL) {
    lock_filesys();
    file_close(file_);
    unlock_filesys();
    t->fd_table[fd] = NULL;
  }
}
/**the algorithm is fairly similar to load executables.
Note for implementation:
1.You must load mapped pages lazily.
2.The file size may not be a multiple of PGSIZE. In that case, you should
  ignore the remaining bytes (ie. don't write them to the file)
3.If the file gets evicted, write all modified pages to disk. When a process
  exits, you must unmap all mapped files.

A call to mmap may fail if 
1.the file open as fd has a length of zero bytes. 
2.addr is not page-aligned or 
3.the range of pages mapped overlaps any existing set of mapped pages, including
  the stack or pages mapped at executable load time. 
4.addr is 0, because some Pintos code assumes virtual page 0 is not mapped.
5.file descriptors 0 and 1, representing console input and output
      
 */
static mapid_t sys_mmap (int fd, void *buffer)
{
  /** verify parameters:
      check page aligned, buffer address, file size, and stack segment
  */
  if (pg_ofs(buffer) != 0 || buffer == NULL || !valid_user_fd(fd) ||
      fd == STDIN_FILENO || fd == STDOUT_FILENO || !is_user_vaddr(buffer) ||
      (buffer >= PHYS_BASE - STACK_SIZE)) {
    return MAP_FAILED;
  }

  struct thread *t = thread_current();
  struct file *file;
  uint32_t read_bytes;        /*mmap file size*/
  uint8_t *upage = buffer;    /* user virtual page start address*/

  // check file existence, create another file structure
  file = file_reopen(t->fd_table[fd]);
  if (file == NULL)
    return MAP_FAILED;

  // check file length
  read_bytes = file_length(file);

  // check page aligned, buffer address, file size, and stack segment
  if (read_bytes == 0) {
    return MAP_FAILED;
  }

  // create a list element for thread's mmap list
  struct mmap  *mmap = malloc (sizeof (struct mmap));
  if (mmap == NULL)
    return MAP_FAILED;
  mmap->mmap_id  = fd;
  mmap->file = file;
  mmap->vaddr = buffer;
  mmap->length = read_bytes;
  list_push_back (&t->mmap_list, &mmap->map_elem);

  off_t file_ofs = 0;
  file_seek (file, file_ofs);
  while (read_bytes > 0) {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    
    /* check overlaps any existing set of mapped pages*/
    struct page *vpage = page_lookup (t, upage);
    if (vpage != NULL) {
      sys_munmap (mmap->mmap_id);
      return MAP_FAILED;
    }
    
    /* Get a page of memory. */
    struct page *page_entry = page_alloc(upage, true); // writable = true
    if (page_entry == NULL) {
      sys_munmap (mmap->mmap_id);
      return MAP_FAILED;
    } else {
      page_entry->file = file;
      page_entry->file_ofs = file_ofs;
      page_entry->read_bytes = page_read_bytes;
      page_entry->zero_bytes = page_zero_bytes;
      page_entry->mmap_id = mmap->mmap_id;	
    }
    
    /* Advance. */
    read_bytes -= page_read_bytes;
    upage += PGSIZE;
    file_ofs += PGSIZE;
  } 
  
  return mmap->mmap_id;
}

static void sys_munmap (mapid_t mapid)
{
  /** verify parameters */
  if (!valid_user_fd(mapid) || mapid == STDIN_FILENO || mapid == STDOUT_FILENO)
    sys_exit(-1);

  struct mmap *mmap = mmap_get_id (mapid);

  if (mmap != NULL) {
    page_munmap (mmap);
    /*close file*/
    // file_close(mmap->file);
    /*free thread's mmap_list*/
    list_remove (&mmap->map_elem);
    free (mmap);
  }
}

static bool sys_chdir (const char *dir)
{
  /** verify parameters */
  if (!access_ok (dir, 0))
    sys_exit(-1);

  struct thread *t = thread_current ();
  char file_name[NAME_MAX + 1];
  struct inode *inode_path = inode_open_path (dir, file_name);
  struct inode *inode = NULL;
  struct dir *working_dir;
  bool success = false;

  if (inode_path != NULL) {
    if (*file_name !='\0') {
      working_dir = dir_open (inode_path);
      dir_lookup (working_dir, file_name, &inode);
      dir_close (working_dir);
    } else {
      inode = inode_path;
    }
  }

  if (inode != NULL) {
    if (inode_is_dir (inode)) {
      dir_close (t->cur_dir);
      t->cur_dir = dir_open (inode);
      success = true;
    } else {
      inode_close (inode);
    }
  }
  return success;
}

static bool sys_mkdir (const char *dir)
{
  /** verify parameters */
  if (!access_ok (dir, 0))
    sys_exit(-1);
  bool success = false;

  lock_filesys();
  success = filesys_mkdir (dir);

  unlock_filesys();
  return success;
  return false;
}

static bool sys_readdir (int fd, char *name)
{
  /** verify parameters */
  if (!access_ok (name, 0) || !valid_user_fd(fd)
      || fd == STDIN_FILENO || fd == STDOUT_FILENO)
    sys_exit(-1);

  struct thread *t = thread_current ();
  struct file *file = t->fd_table[fd];
  struct inode *inode;
  bool success = false;

  if (file != NULL) {
    inode = file_get_inode (file);
    if (inode_is_dir (inode)) {
      success = dir_listdir (file->dir, name);
    }
  }
  return success;
}

static bool sys_isdir (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd) || fd == STDIN_FILENO || fd == STDOUT_FILENO)
    sys_exit(-1);

  struct thread *t = thread_current ();
  bool isdir = false;

  /* Get file info*/
  struct file *file_ = t->fd_table[fd];
  
  if (file_ != NULL)
    isdir = inode_is_dir (file_get_inode (file_));

  return isdir;
}

static int sys_isnumber (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd) || fd == STDIN_FILENO || fd == STDOUT_FILENO)
    sys_exit(-1);
  struct thread *t = thread_current ();
  int inumber = -1;

  /* Get file info*/
  struct file *file_ = t->fd_table[fd];
  
  if (file_ != NULL) {
    lock_filesys();
    inumber = inode_get_inumber (file_get_inode (file_));
    unlock_filesys();
  }

  return inumber;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
