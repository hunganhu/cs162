#include "userprog/syscall.h"
#include <stdio.h>
#include <user/syscall.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool access_ok (const void *, unsigned);
static bool valid_user_fd (int);
static uint32_t read_argument (struct intr_frame *, int);

static void sys_halt (void);
static void sys_exit (int status);
static pid_t sys_exec (const char *file);
static int sys_wait (pid_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned length);
static int sys_write (int fd, const void *buffer, unsigned length);
static void sys_seek (int fd, unsigned position);
static int sys_tell (int fd);
static void sys_close (int fd);

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_no;
  uint32_t arg0, arg1, arg2, arg3;

  arg0 = read_argument(f, 0);
  syscall_no = (int) arg0;

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
    case SYS_MUNMAP:                 /* Remove a memory mapping. */
      break;

    /* Project 4 only. */
    case SYS_CHDIR:                  /* 15 Change the current directory. */
    case SYS_MKDIR:                  /* Create a directory. */
    case SYS_READDIR:                /* Reads a directory entry. */
    case SYS_ISDIR:                  /* Tests if a fd represents a directory. */
    case SYS_INUMBER:                /* 19 Returns the inode number for a fd. */
      break;
    default:
      break;
    }

  //  thread_exit ();
}

/* Check validity of buffer starting at vaddr, with length of size*/
static bool
access_ok (const void * vaddr, unsigned size)
{
  /* If the address exceeds PHYS_BASE, exit -1 */
  if (!is_user_vaddr (vaddr + size)) 
    return false;

  return true;
}
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
  //  if (!access_ok (f->esp + offset * sizeof (void *), 0))
  //    kill_process();

  return *(uint32_t *)(f->esp + offset * sizeof (void *));
}


static void sys_halt (void)
{
  shutdown_power_off ();
}

static void sys_exit (int status)
{
  struct thread *t = thread_current ();
  t->exit_status = status;

  thread_exit ();
}

static pid_t sys_exec (const char *file)
{
  /** verify parameters */
  if (file == NULL)
    sys_exit(-1);

  return (pid_t)-1;
}

static int sys_wait (pid_t pid)
{
  if (pid == TID_ERROR)
    sys_exit(-1);

  return -1;
}

static bool sys_create (const char *file, unsigned initial_size)
{
  /** verify parameters */
  if (file == NULL)
    sys_exit(-1);

  return false;
}

static bool sys_remove (const char *file)
{
  /** verify parameters */
  if (file == NULL)
    sys_exit(-1);

  return false;
}

static int sys_open (const char *file)
{
  /** verify parameters */
  if (file == NULL)
    sys_exit(-1);

  return -1;
}

static int sys_filesize (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd))
    sys_exit(-1);

  return -1;
}

static int sys_read (int fd, void *buffer, unsigned length)
{
  /** verify parameters */
  if (!access_ok (buffer, 0) || !valid_user_fd(fd))
    sys_exit(-1);

  return -1;
}

static int sys_write (int fd, const void *buffer, unsigned length)
{
  /** verify parameters */
  if (!access_ok (buffer, 0) || !valid_user_fd(fd) || fd == STDIN_FILENO)
    sys_exit(-1);

  struct thread *t = thread_current ();
  unsigned byte_written = -1;

  if (fd == STDOUT_FILENO) {
    putbuf(buffer, length);
    byte_written = length;
  } if (fd == STDIN_FILENO) {
    byte_written = -1;
  }
  else {
      /* Get file info*/
      struct file *file_ = t->fd_table[fd];
      off_t offset;

      if (file_ != NULL) {
	/* Synchronize filesys operation */
	//lock_acquire (filesys_lock);
	offset = file_->pos;
	byte_written = file_write_at (file_, buffer, length, offset);
	
	/* Increment position within file for current thread */
	if (byte_written > 0)
	  file_->pos += byte_written;
	
	//lock_release (filesys_lock);
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
    /* Synchronize filesys operation */
    //lock_acquire (filesys_lock);
    file_->pos = position;
    //lock_release (filesys_lock);
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
    /* Synchronize filesys operation */
    //lock_acquire (filesys_lock);
    position = file_->pos;
    //lock_release (filesys_lock);
  }

  return position;
}

static void sys_close (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd))
    sys_exit(-1);

}

