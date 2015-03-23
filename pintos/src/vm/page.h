#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdint.h>
#include <list.h>
#include <user/syscall.h>
#include "filesys/off_t.h"
#include "devices/block.h"

#define PINTOS_CODE_START 0x08048000           //PINTOS text segment start   
#define STACK_SIZE (8 * 1024 * 1024)           //8 MB stack size
#define CODE_BASE ((void *) PINTOS_CODE_START) //virtual address should above it

#define TRACE_ON false
#define DEBUG  if (TRACE_ON) printf
struct mmap 
{
  mapid_t mmap_id;              /* mmap id, same as file fd */
  struct file *file;            /* the file struct of memory map file */
  uint8_t  *vaddr;              /* the start address of virtual memory */
  uint32_t length;              /* the length of mmap file */
  struct list_elem map_elem;    /* the element in thread's mmap list */
};

enum page_type 
  {
    PT_FILE,   /* from file system */
    PT_MMAP,   /* from memory mapped file */
    PT_SWAP,   /* from swap device */
    PT_ZERO    /* new page - all zeros */
  };

/*

*/
struct page
{
  void *vaddr; /* virtual address, key of hash table. */
  struct frame *frame;        /* physical frame assigned*/
  struct thread *thread;      /* page owner */
  struct hash_elem hash_elem; /* Hash table element. */
  bool private;       /*flag for page source, True for swap and false for file*/
  /*--attributes for page from file-------*/
  struct file *file;     /* the file that the page sources */
  off_t file_ofs;        /* starting position of the file */
  uint32_t read_bytes;   /* the size of the page content*/
  uint32_t zero_bytes;   /* the size of padding zeros*/
  bool writable;         /* page writable? */
  mapid_t mmap_id;       /* mmap file id, -1 for file, others for mmap file */
  /*--attributes for page from swap-------*/
  block_sector_t swap_slot; /* swap slot number*/
};

unsigned page_hash_value (const struct hash_elem *pe, void *aux UNUSED);
bool page_hash_less (const struct hash_elem *a, const struct hash_elem *b,
		     void *aux UNUSED);
struct page *page_lookup (struct thread *cur, void *vaddr);
struct page *page_alloc (void *vaddr, bool writable);
void page_release (struct page *vpage);
bool page_in (void *vaddr);
bool page_out (struct page *vpage);
bool page_is_accessed (struct page *vpage);
void page_set_accessed (struct page *vpage, bool accessed);
bool page_is_dirty (struct page *vpage);
void page_destroy (struct hash_elem *e, void *aux UNUSED);
struct mmap *mmap_get_id(mapid_t mapid);
void page_munmap (struct mmap *mmap);

#endif /* vm/page.h */
