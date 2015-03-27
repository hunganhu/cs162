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
  swap_bitmap = bitmap_create( block_size(swap_device));

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
  ASSERT (vpage->frame != NULL);

  DEBUG ("SwapIn=%p, frame=%p, slot=%d.\n", vpage->vaddr,
	 vpage->frame->kpage, vpage->swap_slot);
  lock_acquire (&swap_lock);
  vpage->frame->pinned = true;   //set frame pinned
  // load content of vpage from swap disk
  for (i = 0, buffer = vpage->frame->kpage;
       i < PAGE_BLOCKS;
       i++, buffer += BLOCK_SECTOR_SIZE) {
    block_read (swap_device, vpage->swap_slot + i, buffer);
  
    printf ("SwapIn=%p, frame=%p slot=%d.\n", vpage->vaddr, 
	    buffer, vpage->swap_slot + i);
    hex_dump(0, buffer, BLOCK_SECTOR_SIZE, true);
   
  }
  vpage->frame->pinned = false;   //set frame unpinned
  bitmap_set_multiple (swap_bitmap, vpage->swap_slot, PAGE_BLOCKS, false);
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
  swap_idx = bitmap_scan_and_flip (swap_bitmap, 0, PAGE_BLOCKS, false);
  //bitmap_dump(swap_bitmap);

  if (swap_idx != BITMAP_ERROR) {
    DEBUG ("SwapOut=0x%08"PRIx32", frame=0x%08"PRIx32", slot=%d.\n", 
	   (uint32_t) vpage->vaddr, (uint32_t) vpage->frame->kpage,
	   swap_idx);
    //write content of vpage to swap disk
    vpage->frame->pinned = true;   //set frame pinned
    for (i = 0, buffer = vpage->frame->kpage;
	 i < PAGE_BLOCKS;
	 i++, buffer += BLOCK_SECTOR_SIZE) {
      block_write (swap_device, swap_idx + i, buffer);
      
      if (vpage->private==false && vpage->read_bytes>0) {
	printf ("SwapOut=%p, frame=%p, slot=%d.\n", 
	       vpage->vaddr, buffer, swap_idx + i);
	hex_dump(0, buffer, BLOCK_SECTOR_SIZE, true);
      }
      
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
