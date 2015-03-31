/* pintos/src/vm/page.c
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
#include "userprog/exception.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
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

/*
  Return a page entry of the thread's supplemental page table for the 
  specific virtual address. Return NULL if not found.
 */
struct page *
page_lookup (struct thread *t, void *vaddr)
{
  struct page p;
  struct hash_elem *e;

  p.vaddr = vaddr;
  e = hash_find (&t->supplemental_pages, &p.hash_elem);

  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/** allocate a page entry for vaddr, initialize the page values and  insert
    into thread's supplemental page table.
*/
struct page *page_alloc (void *vaddr, bool writable)
{
  struct page *vpage;
  struct thread *t = thread_current();
  void *page_vaddr = pg_round_down(vaddr); /*the page vaddr is in*/

  vpage = page_lookup(t, page_vaddr);
  if (vpage != NULL)    /* vaddr is already in thread's supplemental pages */
    return vpage;
  /* vaddr not in current supplemental pages */
  vpage = malloc (sizeof (struct page));
  if (vpage == NULL) {
    return NULL;
  }

  /*initial a new page entry */
  vpage->vaddr = page_vaddr;
  vpage->thread = t;
  vpage->writable = writable;
  vpage->frame = NULL;
  vpage->private = false; // page source from file
  vpage->dirty = false;   // initial to clean
  vpage->swap_slot = (block_sector_t) -1;
  vpage->mmap_id = MAP_FAILED;
  vpage->file = NULL;
  vpage->file_ofs = 0;
  vpage->read_bytes = 0;
  vpage->zero_bytes = 0;
 
 /*insert into supplemental pages*/
  hash_insert (&t->supplemental_pages, &vpage->hash_elem);

  return vpage;
}

/* release a page is to delete the entry in thread's supplemental page, reset
   the swap slots and free the resource allocated.
*/
void page_release (struct page *vpage)
{
  struct thread *t = thread_current();
  /** free frame table */
  if (vpage->frame != NULL)
    frame_release (vpage->frame);

  /** free swap slots */
  if (vpage->private)
    swap_clear (vpage);

  /**remove vpage from thread's supplemental page table */
  hash_delete (&t->supplemental_pages, &vpage->hash_elem);
  free(vpage);
}

/* pin a virtual page such that it will not be evicted */
void page_pin (void *page_vaddr)
{
  struct page *vpage;
  struct thread *t = thread_current();

  vpage = page_lookup(t, page_vaddr);

  DEBUG ("PagePin=%p, frame=%p, accessed=%s, dirty=%s, "
	 "private=%s, pinned=%s, file=%p, ofs=%d, read=%d,zero=%d.\n", 
	 vpage->vaddr, vpage->frame->kpage,
	 page_is_accessed (vpage)? "T" : "F",
	 page_is_dirty (vpage)? "T" : "F",
	 vpage->private? "T" : "F",
	 vpage->frame->pinned? "T" : "F",
	 vpage->file, vpage->file_ofs, vpage->read_bytes, vpage->zero_bytes);
 
 if (vpage == NULL) {
   if (page_in (vpage))
     vpage->frame->pinned = true;
 } else if (vpage->frame == NULL) {
   if (page_in (vpage))
     vpage->frame->pinned = true;
 } else {
    vpage->frame->pinned = true;
  }
}

/* Unpin a virtual page such that it can be evicted */
void page_unpin (void *page_vaddr)
{
  struct page *vpage;
  struct thread *t = thread_current();

  vpage = page_lookup(t, page_vaddr);
  ASSERT (vpage != NULL);
  if (vpage->frame != NULL)
    vpage->frame->pinned = false;
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
  struct thread *t = thread_current();
  void *page_vaddr = pg_round_down(vaddr); /*the page that vaddr is in*/
  bool success = false;

  if (is_stack (vaddr, t->stack_pointer)) { // check stack growth
    page_alloc(vaddr, true);
  }
 
  vpage = page_lookup(t, page_vaddr);
  if (vpage == NULL)
    return false;

  ASSERT (vpage->thread == t);

  if (vpage->frame == NULL)
    vpage->frame = frame_alloc(vpage);

  if (vpage->private) {
    // do swap in
    if (SWAP_ON) {
      printf ("SwapIn=%p, frame=%p, slot=%d, private=%s.\n", 
	      vpage->vaddr, vpage->frame->kpage, vpage->swap_slot,
	      vpage->private == true? "T" : "F");
    }
    swap_in(vpage);
    success = true;
  } else if (vpage->file == NULL) {
    // for stack
    vpage->frame->pinned = true;   //set frame pinned
    memset (vpage->frame->kpage, 0, PGSIZE);
    vpage->frame->pinned = false;   //set frame unpinned
    success = true;
  } else {
    vpage->frame->pinned = true;   //set frame pinned

    lock_filesys();
    file_seek (vpage->file, vpage->file_ofs);
    if (file_read (vpage->file, vpage->frame->kpage, vpage->read_bytes) 
	!= (int) vpage->read_bytes) {
      success = false; 
    } else {
      memset (vpage->frame->kpage + vpage->read_bytes, 0, vpage->zero_bytes);
      success = true;
    }
    unlock_filesys();
    // Keep text(writable=false) pinned
    // pass page-merge-seq, page-merge-par, page-merge-stk, page-merge-mm
    if (vpage->writable)
      vpage->frame->pinned = false;   //set frame unpinned
  }

  if (success) {
    /* Add the page to the process's address space. */
    if (pagedir_get_page (t->pagedir, vpage->vaddr) == NULL)
      if (!pagedir_set_page (t->pagedir, vpage->vaddr, 
			     vpage->frame->kpage, vpage->writable))  {
	success = false; 
    }
    DEBUG ("PageIn=%p, frame=%p, accessed=%s, dirty=%s, "
	   "private=%s, file=%p, ofs=%d, read=%d,zero=%d.\n", 
	   vpage->vaddr, vpage->frame->kpage,
	   page_is_accessed (vpage)? "T" : "F",
	   page_is_dirty (vpage)? "T" : "F",
	   vpage->private? "T" : "F",
	   vpage->file, vpage->file_ofs, vpage->read_bytes, vpage->zero_bytes);
  }
  return success;
}
/** page_out algorithm
    1. check the virtual page is valid or not
    2. check the dirty bit by the dirty flag on the thread's page table.
    3. if dirty, write the page content to disk accordig the type of 
       mmap, file, or stack.
    4. for stack, write to swap disk and mark the dirty bit.
    5. for file (private=F), write to swap disk and mark the dirty bit.
    6. for mmap file (file != NULL and mmap_id != -1), write the content to 
       file system and clear the dirty bit.
    7. clear the page table entry to "not present".
 */
bool page_out (struct page *vpage)
{
  //check if the page frame is null
  ASSERT (vpage->frame != NULL);
  ASSERT (vpage->private == false);

  bool success = true;
  struct thread *t = thread_current();

  DEBUG ("PageOut=%p, frame=%p, accessed=%s, dirty=%s, "
	 "private=%s, file=%p, ofs=%d, read=%d,zero=%d.\n", 
	 vpage->vaddr, vpage->frame->kpage,
	 page_is_accessed (vpage)? "T" : "F",
	 page_is_dirty (vpage)? "T" : "F",
	 vpage->private? "T" : "F",
	 vpage->file, vpage->file_ofs, vpage->read_bytes, vpage->zero_bytes);

  vpage->dirty |= page_is_dirty (vpage);
  if (vpage->dirty) {
    if (vpage->file == NULL) { 
      // page source is stack
      swap_out(vpage);
      if (SWAP_ON) {
	// load content of vpage from swap disk
	printf ("SwapOut(STACK)=%p, slot=%d, private=%s.\n ", 
		vpage->vaddr, vpage->swap_slot,
		vpage->private == true? "T" : "F");
      }
    } else if (vpage->file != NULL && vpage->mmap_id == MAP_FAILED){
      // page source is file
      swap_out(vpage);
      if (SWAP_ON) {
	// load content of vpage from swap disk
	printf ("SwapOut(FILE)=%p, slot=%d, private=%s.\n ", 
		vpage->vaddr, vpage->swap_slot,
		vpage->private == true? "T" : "F");
      }
    } else if (vpage->file != NULL && vpage->mmap_id != MAP_FAILED) {
      //page source is mmap, write the dirty page to mmap file
      lock_filesys();
      file_reopen (vpage->file);
      file_seek (vpage->file, vpage->file_ofs);
      if (file_write_at (vpage->file, vpage->vaddr, vpage->read_bytes,
			 vpage->file_ofs) != (int32_t) vpage->read_bytes) { 
	success = false; 
      } else {
	pagedir_set_dirty (t->pagedir, vpage->vaddr, false);
	vpage->dirty = false;
      }
      unlock_filesys();
    }
  }

  if (success) {
    pagedir_clear_page (t->pagedir, vpage->vaddr);
    vpage->frame = NULL;
  }
  return success;
}

/* return the access flag on thread's page table entry */
bool page_is_accessed (struct page *vpage)
{
  //call function pagedir_is_accessed to check if it has been recently 
  //accessed
  return pagedir_is_accessed (vpage->thread->pagedir, vpage->vaddr);
}

/* set the access flag to "accessed" on thread's page table entry */
void page_set_accessed (struct page *vpage, bool accessed)
{
   pagedir_set_accessed (vpage->thread->pagedir, vpage->vaddr, accessed);
}

/* return the dirty flag on thread's page table entry */
bool page_is_dirty (struct page *vpage)
{
  //check if the page frame is null
  // ASSERT (vpage->frame != NULL);
 
  return pagedir_is_dirty (vpage->thread->pagedir, vpage->vaddr);
}

/** hash action function used in hash_clear or hash_destroy */
void page_destroy (struct hash_elem *e, void *aux UNUSED)
{
  struct page *pg = hash_entry (e, struct page, hash_elem);

  /** release frame table */
  if (pg->frame != NULL)
    frame_release (pg->frame);

  /** free swap slots if the page has been swaped out */
  if (pg->private)
    swap_clear (pg);
  free (pg);
}

/*
  Return the entry of thread's mmap list by mapid. Return null if mapid is not
  found
*/
struct mmap *mmap_get_id(mapid_t mapid) 
{
  struct thread *t = thread_current();
  struct list_elem *e;
  struct mmap *mmap = NULL;

  // search mmap struct for the corresponding mapid
  for (e = list_begin (&t->mmap_list); e != list_end (&t->mmap_list); 
       e = list_next (e)) {
    mmap = list_entry (e, struct mmap, map_elem);
    if (mmap->mmap_id == mapid)
      return mmap;
  }
  return NULL;
}
/*
  Remove a mmap file. Write the content in memory to file system and free the 
  allocated resource.
*/
void page_munmap (struct mmap *mmap)
{
  /*free thread's supplemental page table*/
  struct thread *t = thread_current();
  uint8_t *upage = mmap->vaddr;
  uint32_t read_bytes = mmap->length;

  while (read_bytes > 0) {
    /* Calculate how to fill this page. read PAGE_READ_BYTES bytes from FILE*/
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    
    /* check overlaps any existing set of mapped pages*/
    struct page *vpage = page_lookup (t, upage);
    if (vpage != NULL) {
      if (vpage->frame != NULL) {
	if (page_is_dirty(vpage)) {
	  // write page content back to file
	  file_write_at (vpage->file, vpage->vaddr, vpage->read_bytes,
			 vpage->file_ofs);
	}
      }
      page_release(vpage);
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    upage += PGSIZE;
   }
}
