/* swap.c
 */

#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "threads/synch.h"
#include "devices/block.h"

#include "vm/swap.h"

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
  swap_bitmap = bitmap_create(block_size(swap_device)); //number of sectors
  if (swap_bitmap == NULL) {
    PANIC("Swap device initial fails.\n");
    return;
  }

  lock_init(&swap_lock);
}

bool swap_in(void)
{
  return false;
}

bool swap_out(void)
{
  return false;
}
