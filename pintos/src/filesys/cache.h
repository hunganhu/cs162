#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <hash.h>
#include <stdint.h>
#include <list.h>
#include <user/syscall.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_ON false
#define CDEBUG  if (CACHE_ON) printf

#define BUFFER_CACHE_SIZE 64
#define CACHE_DELAYED 0x1  /* the buffer is delayed write */
#define CACHE_BUSY    0x2  /* the buffer is selected to be r/w, cannot evict */
//#define CACHE_FLUSH   0x4  /* the buffer is flushing */
//#define CACHE_WAIT    0x8  /* the buffer is requested by other processes */
struct shared_lock
{
  int i;
  struct lock lock;
  struct condition cond;
};

struct cache_entry
{
  int seq;                    /* sequence of the cache entry */
  struct hash_elem hash_elem; /* An element in the buffer_cache hash table */
  struct list_elem list_elem; /* Member in lru list */
  block_sector_t sector;      /* Data block number, the key of hash table */
  int  status;                /* Status of the cache entry */
  struct semaphore sema_buf;  /* event to indicate this buffer is available */
  struct shared_lock lock_shared;/*monitor for multiple readers and one writer*/
  char data[BLOCK_SECTOR_SIZE]; /* Actual data read from the block */
};

unsigned cache_hash_value (const struct hash_elem *ce, void *aux UNUSED);
bool cache_hash_less (const struct hash_elem *a, const struct hash_elem *b,
		      void *aux UNUSED);
struct cache_entry *cache_lookup (block_sector_t sector);

void cache_init (void);
struct cache_entry *cache_get_block (block_sector_t sector);
void cache_release (struct cache_entry *cache);
void cache_lock (struct cache_entry *ce);
void cache_unlock (struct cache_entry *ce);
void cache_evict (void);
void cache_flush_buffer (struct cache_entry *buffer);
void cache_flush_cache (void);
void cache_flush_task (void *AUX UNUSED);
void cache_block_read (struct block *block, block_sector_t sector, void *data);
void cache_block_write (struct block *block, block_sector_t sector,
			const void *data);
bool buffer_is_delayed (struct cache_entry *buffer);
void buffer_set_delayed (struct cache_entry *buffer, bool flag);
bool buffer_is_busy (struct cache_entry *buffer);
void buffer_set_busy (struct cache_entry *buffer, bool flag);

/*
Readers-Writers Problem
• Multiple threads may access data
  - Readers – will only observe, not modify data
  - Writers – will change the data
• Goal: allow multiple readers or one single writer
  - Thus, lock can be shared amongst concurrent readers
• Can implement with other primitives
  - Keep integer i – # or readers or -1 if held by writer
  - Protect i with mutex
  - Sleep on condition variable when can’t get lock
*/
void init_shared (struct shared_lock *s);
void acquire_shared (struct shared_lock *s);
void acquire_exclusive (struct shared_lock *s);
void release_shared (struct shared_lock *s);
void release_exclusive (struct shared_lock *s);

#endif /* filesys/cache.h */
