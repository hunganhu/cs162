#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/off_t.h"
#include "devices/block.h"

#define PINTOS_CODE_START 0x08048000           //PINTOS text segment start   
#define STACK_SIZE (8 * 1024 * 1024)           //8 MB stack size
#define CODE_BASE ((void *) PINTOS_CODE_START) //virtual address should above it

enum page_type 
  {
    PT_FILE,   // from file system
    PT_MMAP,   // from memory mapped file
    PT_SWAP,   // from swap device
    PT_ZERO    // new page, all zeros
  };

struct page
{
  void *vaddr; /* virtual address, key of hash table. */
  struct frame *frame;        /* physical frame assigned*/
  struct thread *thread;      /* page owner */
  struct hash_elem hash_elem; /* Hash table element. */
  bool private;       /*flag for page source, True for swap and false for file*/
  //--attributes for page from file-------
  struct file *file;     /* the file that the page sources */
  off_t file_ofs;        /* starting position of the file */
  uint32_t read_bytes;   /* the size of the page content*/
  uint32_t zero_bytes;   /* the size of padding zeros*/
  bool writable;         /* page writable? */
  //--attributes for page from swap-------
  block_sector_t swap_sector; /* swap slot number*/
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

#endif /* vm/page.h */
