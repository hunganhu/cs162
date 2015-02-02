#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <user/syscall.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
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
static bool put_user (uint8_t *, uint8_t);

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_no;
  uint32_t arg0, arg1, arg2, arg3;

  arg0 = read_argument(f, 0);
  syscall_no = (int) arg0;

  /* Synchronize syscall operation */
  //  lock_acquire (&syscall_lock);
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
      f->eax = sys_mmap((int) arg1, (void *) arg2);
      break;
    case SYS_MUNMAP:                 /* Remove a memory mapping. */
      arg1 = read_argument(f, 1);
      sys_munmap((mapid_t) arg1);
      break;

    /* Project 4 only. */
    case SYS_CHDIR:                  /* 15 Change the current directory. */
      arg1 = read_argument(f, 1);
      f->eax = sys_chdir((char *) arg1);
      break;
    case SYS_MKDIR:                  /* Create a directory. */
      arg1 = read_argument(f, 1);
      f->eax = sys_mkdir((char *) arg1);
      break;
    case SYS_READDIR:                /* Reads a directory entry. */
      arg1 = read_argument(f, 1);
      arg2 = read_argument(f, 2);
      f->eax = sys_readdir((int) arg1, (char *) arg2);
      break;
    case SYS_ISDIR:                  /* Tests if a fd represents a directory. */
      arg1 = read_argument(f, 1);
      f->eax = sys_isdir((int) arg1);
      break;
    case SYS_INUMBER:                /* 19 Returns the inode number for a fd. */
      arg1 = read_argument(f, 1);
      f->eax = sys_isnumber((int) arg1);
      break;
    default:
      break;
    }
  //  lock_release(&syscall_lock);
}

/* Check validity of buffer starting at vaddr, with length of size*/
static bool
access_ok (const void * vaddr, unsigned size)
{
  struct thread *t = thread_current();
  uint32_t *pd = t->pagedir;

  /* If the address exceeds PHYS_BASE, or address is not mapped,  exit -1 */
  if (!is_user_vaddr (vaddr + size) ||
      !is_user_vaddr (vaddr) ||
      pagedir_get_page (pd, vaddr) == NULL || 
      pagedir_get_page (pd, vaddr + size) == NULL)
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
  // the name of file cannot be empty and cannot be existed
  if (filesys_open(file) == NULL)
    success = filesys_create (file, initial_size);
  else
    success = false;
  return success;
}

static bool sys_remove (const char *file)
{
  /** verify parameters */
  if (!access_ok(file, 0))
    sys_exit(-1);

  bool success = false;
  if (filesys_open(file) != NULL) {
    success = filesys_remove (file);
  }
  return success;

}

static int sys_open (const char *file)
{
  /** verify parameters */
  if (file == NULL || !access_ok(file, 0))
    sys_exit(-1);

  struct thread *t = thread_current ();
 
  /* Get file info*/
  struct file *file_ = filesys_open(file);
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
    size = file_length(file_);
  }
  return size;
}

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

    if (file_ != NULL)
      byte_read = file_read (file_, buffer, size);
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

    if (file_ != NULL)
      byte_written = file_write (file_, buffer, size);
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
  
  if (file_ != NULL)
    file_seek(file_, position);
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
  
  if (file_ != NULL)
    position = file_tell(file_);

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
    file_close(file_);
    t->fd_table[fd] = NULL;
  }
}

static mapid_t sys_mmap (int fd, void *buffer)
{
  /** verify parameters */
  if (!access_ok (buffer, 0) || !valid_user_fd(fd)
      || fd == STDIN_FILENO || fd == STDOUT_FILENO)
    sys_exit(-1);

  mapid_t mapid = -1;

  return mapid;
}

static void sys_munmap (mapid_t mapid)
{
  /** verify parameters */
  if (!valid_user_fd(mapid) || mapid == STDIN_FILENO || mapid == STDOUT_FILENO)
    sys_exit(-1);
}

static bool sys_chdir (const char *dir)
{
  /** verify parameters */
  if (!access_ok (dir, 0))
    sys_exit(-1);

  return false;
}

static bool sys_mkdir (const char *dir)
{
  /** verify parameters */
  if (!access_ok (dir, 0))
    sys_exit(-1);

  return false;
}

static bool sys_readdir (int fd, char *name)
{
  /** verify parameters */
  if (!access_ok (name, 0) || !valid_user_fd(fd)
      || fd == STDIN_FILENO || fd == STDOUT_FILENO)
    sys_exit(-1);

  return false;
}

static bool sys_isdir (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd) || fd == STDIN_FILENO || fd == STDOUT_FILENO)
    sys_exit(-1);

  return false;
}

static int sys_isnumber (int fd)
{
  /** verify parameters */
  if (!valid_user_fd(fd) || fd == STDIN_FILENO || fd == STDOUT_FILENO)
    sys_exit(-1);

  int inode = -1;

  return inode;
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
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}
