/* swap.c
 */

#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/block.h"

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
}

void swap_in(struct page *vpage)
{
  int i;
  void *buffer;

  ASSERT (vpage != NULL);
  ASSERT (vpage->private == true);
  ASSERT (vpage->swap_slot != BITMAP_ERROR);

  lock_acquire (&swap_lock);
  // restore content of vpage from swap disk
  for (i = vpage->swap_slot * PAGE_BLOCKS, buffer = vpage->frame->kpage;
       i < PAGE_BLOCKS;
       i++, buffer += BLOCK_SECTOR_SIZE) {
    block_read (swap_device, i, buffer);
  }

  //update vpage meta data
  vpage->private = false;
  vpage->swap_slot = (block_sector_t) -1;

  ASSERT (bitmap_test (swap_bitmap, vpage->swap_slot));
  bitmap_set (swap_bitmap, vpage->swap_slot, false);

  lock_release (&swap_lock);
}

block_sector_t swap_out(struct page *vpage)
{
  size_t swap_idx;
  int i;
  void *buffer;

  lock_acquire (&swap_lock);
  swap_idx = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);

  if (swap_idx != BITMAP_ERROR) {
    //write content of vpage to swap disk
    for (i = swap_idx * PAGE_BLOCKS, buffer = vpage->frame->kpage;
	 i < PAGE_BLOCKS;
	 i++, buffer += BLOCK_SECTOR_SIZE) {
      block_write (swap_device, i, buffer);
    }
    //update vpage meta data
    vpage->private = true;
    vpage->swap_slot = swap_idx;
  }
  lock_release (&swap_lock);

  return swap_idx;
}

void swap_clear (struct page *vpage)
{
  lock_acquire (&swap_lock);

  ASSERT (bitmap_test (swap_bitmap, vpage->swap_slot));
  bitmap_set (swap_bitmap, vpage->swap_slot, false);

  lock_release (&swap_lock);
}
