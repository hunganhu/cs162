#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <user/syscall.h>

void syscall_init (void);
void lock_filesys (void);
void unlock_filesys (void);


#endif /* userprog/syscall.h */
