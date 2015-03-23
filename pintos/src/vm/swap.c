/* swap.c
 */

#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/block.h"

#include "vm/page.h"
#include "vm/swap.h"

#define PAGE_BLOCKS (PGSIZE / BLOCK_SECTOR_SIZE)
static struct block  *swap_device;
static struct bitmap *swap_bitmap;
static struct lock   swap_lock;

void swap_init(void)
{
  swap_device = block_get_role(BLOCK_SWAP);
  if (swap_device == NULL) {
    PANIC ("Swap device does not exist.\n");
    return;
  }
  /* Create bitmap table based on page number */
  swap_bitmap = bitmap_create( block_size(swap_device)/ PAGE_BLOCKS);

  if (swap_bitmap == NULL) {
    PANIC("Swap device initial fails.\n");
    return;
  }
  lock_init(&swap_lock);
  DEBUG ("Swap Init, Disk size=%d sector, Bitmap size=%d, Page blocks=%d.\n",
	  block_size(swap_device), bitmap_size(swap_bitmap), PAGE_BLOCKS);
  //bitmap_dump(swap_bitmap);
}

void swap_in(struct page *vpage)
{
  int i = 0;
  void *buffer;

  ASSERT (vpage != NULL);
  ASSERT (vpage->private == true);
  ASSERT (vpage->swap_slot != BITMAP_ERROR);
  ASSERT (bitmap_all (swap_bitmap, vpage->swap_slot, 1));

  DEBUG ("SwapIn=0x%08"PRIx32", slot=%d.\n",
	  (uint32_t) vpage->vaddr, vpage->swap_slot);
  lock_acquire (&swap_lock);
  vpage->frame->pinned = true;   //set frame pinned
  // restore content of vpage from swap disk
  for (i = vpage->swap_slot * PAGE_BLOCKS, buffer = vpage->frame->kpage;
       i < PAGE_BLOCKS;
       i++, buffer += BLOCK_SECTOR_SIZE) {
    block_read (swap_device, i, buffer);
  }
  vpage->frame->pinned = false;   //set frame unpinned
  bitmap_set (swap_bitmap, vpage->swap_slot, false);
  lock_release (&swap_lock);

  //update vpage meta data
  vpage->private = false;
  vpage->swap_slot = (block_sector_t) -1;

  //bitmap_dump(swap_bitmap);
}

block_sector_t swap_out(struct page *vpage)
{
  size_t swap_idx;
  int i;
  void *buffer;

  lock_acquire (&swap_lock);
  swap_idx = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  //bitmap_dump(swap_bitmap);

  if (swap_idx != BITMAP_ERROR) {
    DEBUG ("SwapOut=0x%08"PRIx32", frame=0x%08"PRIx32", slot=%d.\n", 
	   (uint32_t) vpage->vaddr, (uint32_t) vpage->frame->kpage,
	   swap_idx);
    //write content of vpage to swap disk
    vpage->frame->pinned = true;   //set frame pinned
    for (i = swap_idx * PAGE_BLOCKS, buffer = vpage->frame->kpage;
	 i < PAGE_BLOCKS;
	 i++, buffer += BLOCK_SECTOR_SIZE) {
      block_write (swap_device, i, buffer);
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

void swap_clear (struct page *vpage)
{
  ASSERT (vpage->private);
  
  DEBUG ("SwapClear=0x%08"PRIx32", frame=0x%08"PRIx32", slot=%d.\n", 
	 (uint32_t) vpage->vaddr, (uint32_t) vpage->frame->kpage,
	 vpage->swap_slot);
 
  lock_acquire (&swap_lock);

  ASSERT (bitmap_all (swap_bitmap, vpage->swap_slot, 1));
  bitmap_set (swap_bitmap, vpage->swap_slot, false);

  lock_release (&swap_lock);
}
