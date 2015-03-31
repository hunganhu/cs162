#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/page.h"
#include "vm/frame.h"
#define SWAP_ON false
#define PAGE_BLOCKS (PGSIZE / BLOCK_SECTOR_SIZE)

void swap_init(void);
void swap_in(struct page *vpage);
block_sector_t swap_out(struct page *vpage);
void swap_clear (struct page *vpage);

#endif /* vm/swap.h */
