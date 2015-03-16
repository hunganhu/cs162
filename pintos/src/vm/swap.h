#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/page.h"
#include "vm/frame.h"

void swap_init(void);
bool swap_in(void);
bool swap_out(void);

#endif /* vm/swap.h */
