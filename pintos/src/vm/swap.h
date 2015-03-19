#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/page.h"
#include "vm/frame.h"

void swap_init(void);
void swap_in(struct page *vpage);
block_sector_t swap_out(struct page *vpage);
void swap_clear (struct page *vpage);

#endif /* vm/swap.h */
