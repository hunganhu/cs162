/* pintos/src/vm/frame.c
   Provide programs with free frames when needed
*/
#include <stdio.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"

struct list frame_table; //track system frames usage to help with eviction. 
struct lock frame_lock;
struct list_elem *clock_hand;

/**Initializes the frame table.  */
void frame_init (void)
{
  void *kpage;
  struct frame *frame;
  //  int i = 0;

  list_init(&frame_table);
  lock_init(&frame_lock);
  clock_hand =  list_begin (&frame_table);

  /* initial frame table to allocate all available pages in the user pool */
  DEBUG ("Initial frames.\n");
  lock_acquire (&frame_lock);
  while ((kpage = palloc_get_page(PAL_USER|PAL_ZERO)) != (void *)NULL) {
    frame = malloc(sizeof(struct frame)); /*malloc a frame*/
    /*initial frame values*/
    if (frame != NULL) {
      frame->kpage = kpage; 
      frame->vpage = NULL;
      frame->pinned = false;
    } else {
      PANIC ("Frame initial fails.\n");
    }
    /*append to frame table list*/
    list_push_back(&frame_table, &frame->frame_elem);
    //DEBUG ("Frame[%03d]=0x%08"PRIx32".\n", i++, (uint32_t) frame->kpage);
  }
  lock_release (&frame_lock);  
}

/**Obtains and returns a free page. the frames used for user pages should 
   be obtained from the “user pool,” by calling palloc_get_page(PAL_USER). 
   You must use PAL_USER to avoid allocating from the “kernel pool”.

   If no free page is available, call frame_get_evitor to evict a frame.
*/
struct frame *frame_alloc (struct page *vpage)
{
  struct list_elem *e;
  struct frame *frame;

  for (e = list_begin (&frame_table); e != list_end (&frame_table); 
       e = list_next (e)) {
    frame = list_entry(e, struct frame, frame_elem);
    if (frame->vpage == NULL) {
      frame->vpage = vpage;
       return frame;
    }
  }

  /* no free frame found */
  frame = frame_victim (vpage);
  //  DEBUG ("Alloc Frame=0x%08"PRIx32".\n",(uint32_t) frame->kpage);
  return frame;
}

/**Frees the page and the corresponging frame in frame table. */
void frame_release (struct frame *frame)
{
  lock_acquire (&frame_lock);
  frame->vpage = NULL;
  lock_release (&frame_lock);
}

/** Select an evicted frame if no free frames in frame table.
    The process of eviction comprises roughly the following steps:
    1. Choose a frame to evict, using second chance (LRU) replacement algorithm.
       The “accessed” and “dirty” bits in the page table, described below, will 
       come in handy.
    2. Remove references to the frame from any page table that refers to it.
       Unless you have implemented sharing, only a single page should refer to 
       a frame at any given time.
    3. If necessary, write the page to the file system or to swap.
 */
struct frame *frame_victim(struct page *vpage)
{
  struct list_elem *e;
  struct frame *frame = NULL;
  bool found = false;
  struct thread *cur = thread_current();

  lock_acquire (&frame_lock);
  while (!found) { //select a victim frame, second chance algorithm
    for (e = clock_hand; e != list_end (&frame_table); 
	 e = list_next (e)) {
      frame = list_entry(e, struct frame, frame_elem);
      //consider only not pinned frames and frame owner is me
      if (!frame->pinned && (frame->vpage->thread == cur)) {
      //if (!frame->pinned) {
	if (page_is_accessed (frame->vpage)) {
	  page_set_accessed (frame->vpage, false);
	} else {
	  found = true;
	  clock_hand = e;
	  break;
	}
      }
    }
    if (!found)
      clock_hand = list_begin (&frame_table);
  }
  lock_release (&frame_lock);

  frame->pinned = true;
  page_out (frame->vpage);
  frame->pinned = false;

  // return a frame with the new page
  frame->vpage = vpage;
  /*
  DEBUG ("Return Frame=0x%08"PRIx32", Vpage==0x%08"PRIx32","
	  " accessed=%s, dirty=%s.\n", 
	  (uint32_t) frame->kpage, (uint32_t) frame->vpage->vaddr,
	  page_is_accessed (vpage) ? "T" : "F",
	  page_is_dirty (vpage) ? "T" : "F");
  */
  return frame;
}

