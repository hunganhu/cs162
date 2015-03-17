/* page.c
*/
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

/* Returns a hash value for page p. */
unsigned
page_hash_value (const struct hash_elem *pe, void *aux UNUSED)
{
  const struct page *p = hash_entry (pe, struct page, hash_elem);
  return hash_bytes (&p->vaddr, sizeof p->vaddr);
}

/* Returns true if page a precedes page b. */
bool
page_hash_less (const struct hash_elem *a, const struct hash_elem *b,
		void *aux UNUSED)
{
  const struct page *pa = hash_entry (a, struct page, hash_elem);
  const struct page *pb = hash_entry (b, struct page, hash_elem);
  return pa->vaddr < pb->vaddr;
}

/**
 */
struct page *
page_lookup (struct thread *cur, void *vaddr)
{
  struct page p;
  struct hash_elem *e;

  p.vaddr = vaddr;
  e = hash_find (&cur->supplemental_pages, &p.hash_elem);

  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

struct page *page_alloc (void *vaddr, bool writable)
{
  struct page *vpage;
  struct thread *cur = thread_current();
  void *page_vaddr = pg_round_down(vaddr); /*the page vaddr is in*/

  vpage = page_lookup(cur, page_vaddr);
  if (vpage == NULL) { /* vaddr not in current supplemental pages */
    vpage = malloc (sizeof (struct page));
    if (vpage == NULL) {
      return NULL;
    }
  }
  /*initial a new page entry */
  vpage->vaddr = page_vaddr;
  vpage->thread = cur;
  vpage->writable = writable;
  vpage->frame = NULL;
  vpage->private = false; // page source from file
  vpage->swap_sector = (block_sector_t) -1;
  vpage->mmap_id = MAP_FAILED;
 
 /*insert into supplemental pages*/
  hash_insert (&cur->supplemental_pages, &vpage->hash_elem);

  return vpage;
}

void page_release (struct page *vpage)
{
  struct thread *cur = thread_current();
  /** free frame table */
  if (vpage->frame != NULL)
    frame_release (vpage->frame);

  /** todo: free swap slots */
  hash_delete (&cur->supplemental_pages, &vpage->hash_elem);
  free(vpage);
  /*delete page table entry in pagedir ?*/
}

/** page_in algorithm
    1. check the virtual address is pre-defined in thread's supplemental 
       page table.
    2. if not defined in supplemental page table, then check the address is
       in the range of stack segment (PHYS_BASE - STACK_SIZE, PHYS_BASE).
    3. if in stack range, allocate a free frame for the virtual page.
    4. if not in stack range, the virtual address is invalid and return failed.
    5. if defined in page table, load the page content by swap, file, or stack.
    6. for stack, assigns a zero page
    7. for file (private=F), read the content from the file 
       (file_id, offset, length).
    8. for swap (private=T), read the content from swap device(block_sector).
 */
bool page_in (void *vaddr)
{
  struct page *vpage;
  struct thread *cur = thread_current();
  void *page_vaddr = pg_round_down(vaddr); /*the page that vaddr is in*/
  bool success = false;

  vpage = page_lookup(cur, page_vaddr);
  if (vpage == NULL && 
      (vaddr < PHYS_BASE) && (vaddr >= PHYS_BASE - STACK_SIZE)) {
    // for stack increament
    vpage = page_alloc (vaddr, true);
  }
  
  if (vpage == NULL)
    return false;

  ASSERT (vpage->thread == cur);

  if (vpage->frame == NULL)
    vpage->frame = frame_alloc(vpage);

  if (vpage->private) {
    // do swap in
    if (swap_in(vpage->swap_sector)) {
      success = true;
    }
  } else if (vpage->file == NULL) { 
    // for stack
    memset (vpage->frame->kpage, 0, PGSIZE);
    success = true;
  } else {
    file_seek (vpage->file, vpage->file_ofs);
    if (file_read (vpage->file, vpage->frame->kpage, vpage->read_bytes) 
	!= (int) vpage->read_bytes) {
      success = false; 
    } else {
      memset (vpage->frame->kpage + vpage->read_bytes, 0, vpage->zero_bytes);
      success = true;
    }
  }
  if (success) {
    /* Add the page to the process's address space. */
    if (!pagedir_set_page (cur->pagedir, vpage->vaddr, 
			   vpage->frame->kpage, vpage->writable))  {
      success = false; 
    }
  }
  return success;
}

bool page_out (struct page *vpage)
{
  //check if the page frame is null
  ASSERT (vpage->frame != NULL);
  ASSERT (vpage->private == false);

  bool success = false;
  struct thread *t = thread_current();

  if (page_is_dirty(vpage)) {
    if (vpage->file == NULL) { 
      // page source is stack
      swap_out(vpage);
      success = true;
    } else if (vpage->file != NULL && vpage->mmap_id == MAP_FAILED){
      // page source is file
      swap_out(vpage);
      success = true;
    } else if (vpage->file != NULL && vpage->mmap_id != MAP_FAILED) {
      //page source is mmap, write the dirty page to mmap file
      file_reopen (vpage->file);
      file_seek (vpage->file, vpage->file_ofs);
      if (file_write_at (vpage->file, vpage->vaddr, vpage->read_bytes,
			 vpage->file_ofs) != vpage->read_bytes) { 
	success = false; 
      } else {
	pagedir_set_dirty (t->pagedir, vpage->vaddr, false);
	success = true;
      }
    }
    if (success)
      pagedir_clear_page (t->pagedir, vpage->vaddr);
    success = true;
  }
  return success;
}

bool page_is_accessed (struct page *vpage)
{
  //check if the page frame is null
  ASSERT (vpage->frame != NULL);
  //ASSERT (lock_held_by_current_thread (&vpage->frame->lock));
  //call function pagedir_is_accessed to check if it has been recently 
  //accessed
  return pagedir_is_accessed (vpage->thread->pagedir, vpage->vaddr);
}

void page_set_accessed (struct page *vpage, bool accessed)
{
   pagedir_set_accessed (vpage->thread->pagedir, vpage->vaddr, accessed);
}

bool page_is_dirty (struct page *vpage)
{
  //check if the page frame is null
  ASSERT (vpage->frame != NULL);
 
  return pagedir_is_dirty (vpage->thread->pagedir, vpage->vaddr);
}

/** hash action function used in hash_clear or hash_destroy */
void page_destroy (struct hash_elem *e, void *aux UNUSED)
{
  struct page *pg = hash_entry (e, struct page, hash_elem);
  //  printf("page =0x%08"PRIx32" {vaddr=0x%08"PRIx32", frame=0x%08"PRIx32"}\n",
  //	 (unsigned)pg, (unsigned)pg->vaddr, (unsigned)pg->frame);

  /** release frame table */
  if (pg->frame != NULL)
    frame_release (pg->frame);
  //  printf("page =0x%08"PRIx32" {vaddr=0x%08"PRIx32", frame=0x%08"PRIx32"}\n",
  //	 (unsigned)pg, (unsigned)pg->vaddr, (unsigned)pg->frame);
  /** todo: free swap slots */
  free (pg);
}
