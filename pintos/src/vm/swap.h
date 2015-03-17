#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/page.h"
#include "vm/frame.h"

void swap_init(void);
bool swap_in(block_sector_t swap_sector);
block_sector_t swap_out(struct page *vpage);
#endif /* vm/swap.h */
