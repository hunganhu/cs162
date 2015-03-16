#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include <list.h>
#include "threads/palloc.h"

/** The frame table is a link list to track global system frame usage to 
    help with eviction. 
*/
struct frame
{
  void *kpage;                 /* the kernel page from user pool */
  struct page *vpage;          /* user virtual page, initial NULL */
  bool accessed;               /* Is the page accessed */     
  bool dirty;                  /* Has the page been written */
  bool pinned;                 /* Is the page pinned to disallow eviction */
  struct list_elem frame_elem; /* list element to link in frame table list */
};

void frame_init (void);
struct frame *frame_alloc (struct page *vpage);
void frame_release (struct frame *);
struct frame *frame_victim(struct page *vpage);

#endif /* vm/frame.h */
