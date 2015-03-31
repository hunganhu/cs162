/* pintos/src/vm/swap.c
 */

#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/block.h"

#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct block  *swap_device;
static struct bitmap *swap_bitmap;
static struct lock   swap_lock;
/* 
  Initial swap device
  1. assign swap device
  2. create swap slot table by bitmap
  3. initial lock
*/
void swap_init(void)
{
  swap_device = block_get_role(BLOCK_SWAP);
  if (swap_device == NULL) {
    PANIC ("Swap device does not exist.\n");
    return;
  }
  /* Create bitmap table based on page number */
  swap_bitmap = bitmap_create (block_size(swap_device));

  if (swap_bitmap == NULL) {
    PANIC("Swap device initial fails.\n");
    return;
  }
  lock_init (&swap_lock);
  DEBUG ("Swap Init, Disk size=%d sector, Bitmap size=%d, Page blocks=%d.\n",
	  block_size(swap_device), bitmap_size(swap_bitmap), PAGE_BLOCKS);
  //bitmap_dump(swap_bitmap);
}
/*
   Load the content of the virtual page from swap disk, and clear the swap
   slots it occupied.
 */
void swap_in(struct page *vpage)
{
  int i = 0;
  void *buffer;

  ASSERT (vpage != NULL);
  ASSERT (vpage->private == true);
  ASSERT (vpage->swap_slot != BITMAP_ERROR);
  ASSERT (bitmap_all (swap_bitmap, vpage->swap_slot, PAGE_BLOCKS));
  ASSERT (vpage->frame != NULL);

  lock_acquire (&swap_lock);
  vpage->frame->pinned = true;   //set frame pinned
  // load content of vpage from swap disk
  for (i = 0, buffer = vpage->frame->kpage;
       i < PAGE_BLOCKS;
       i++, buffer += BLOCK_SECTOR_SIZE) {
    block_read (swap_device, vpage->swap_slot + i, buffer);
  }
  vpage->frame->pinned = false;   //set frame unpinned
  bitmap_set_multiple (swap_bitmap, vpage->swap_slot, PAGE_BLOCKS, false);
  lock_release (&swap_lock);

  //update vpage meta data
  vpage->private = false;
  vpage->swap_slot = (block_sector_t) -1;
  //bitmap_dump(swap_bitmap);
}
/*
  Allocate free swap slots and write content of virtual page to swap disk
 */
block_sector_t swap_out(struct page *vpage)
{
  size_t swap_idx;
  int i;
  void *buffer;

  lock_acquire (&swap_lock);
  swap_idx = bitmap_scan_and_flip (swap_bitmap, 0, PAGE_BLOCKS, false);

  if (swap_idx != BITMAP_ERROR) {
    //write content of vpage to swap disk
    vpage->frame->pinned = true;   //set frame pinned
    for (i = 0, buffer = vpage->frame->kpage;
	 i < PAGE_BLOCKS;
	 i++, buffer += BLOCK_SECTOR_SIZE) {
      block_write (swap_device, swap_idx + i, buffer);
    }
    vpage->frame->pinned = false;   //set frame unpinned
    //update vpage meta data
    vpage->private = true;
    vpage->swap_slot = swap_idx;
    vpage->frame = NULL;
  }
  lock_release (&swap_lock);

  return swap_idx;
}

/*
  Reset the swap slots that a virtual page has been resident in. It is called
  when a process exits and destroy is page table.
 */
void swap_clear (struct page *vpage)
{
  ASSERT (vpage->private);
  
  DEBUG ("SwapClear=0x%08"PRIx32", frame=0x%08"PRIx32", slot=%d.\n", 
	 (uint32_t) vpage->vaddr, (uint32_t) vpage->frame->kpage,
	 vpage->swap_slot);
 
  lock_acquire (&swap_lock);

  ASSERT (bitmap_all (swap_bitmap, vpage->swap_slot, PAGE_BLOCKS));
  bitmap_set_multiple (swap_bitmap, vpage->swap_slot, PAGE_BLOCKS, false);

  lock_release (&swap_lock);
}
