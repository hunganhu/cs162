#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <user/syscall.h>
#include <stdint.h>
#include <list.h>

#ifdef VM
struct mmap 
{
  mapid_t mmap_id;              /* mmap id, same as file fd */
  struct file *file;            /* the file struct of memory map file */
  uint8_t  *vaddr;              /* the start address of virtual memory */
  uint32_t length;              /* the length of mmap file */
  struct list_elem map_elem;    /* the element in thread's mmap list */
};
#endif

void syscall_init (void);


#endif /* userprog/syscall.h */
