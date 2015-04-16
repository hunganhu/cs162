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
#define CACHE_FLUSH   0x4  /* the buffer is flushing */
#define CACHE_WAIT    0x8  /* the buffer is requested by other processes */
 
struct cache_entry
{
  int seq;                    /* sequence of the cache entry */
  struct hash_elem hash_elem; /* An element in the buffer_cache hash table */
  struct list_elem list_elem; /* Member in lru list */
  block_sector_t sector;      /* Data block number, the key of hash table */
  char data[BLOCK_SECTOR_SIZE]; /* Actual data read from the block */
  int  status;                /* Status of the cache entry */
  struct semaphore sema_buf;  /* event to indicate this buffer is available */
};

unsigned cache_hash_value (const struct hash_elem *ce, void *aux UNUSED);
bool cache_hash_less (const struct hash_elem *a, const struct hash_elem *b,
		      void *aux UNUSED);
struct cache_entry *cache_lookup (block_sector_t sector);

void cache_init (void);
struct cache_entry *cache_get_buffer (block_sector_t sector);
void cache_release (struct cache_entry *cache);
void cache_lock (struct cache_entry *ce);
void cache_unlock (struct cache_entry *ce);
void cache_evict (void);
void cache_flush_buffer (struct cache_entry *buffer);
void cache_flush_cache (void);
void cache_block_read (struct block *block, block_sector_t sector, void *data);
void cache_block_write (struct block *block, block_sector_t sector,
			const void *data);
bool buffer_is_delayed (struct cache_entry *buffer);
void buffer_set_delayed (struct cache_entry *buffer, bool flag);
bool buffer_is_busy (struct cache_entry *buffer);
void buffer_set_busy (struct cache_entry *buffer, bool flag);
//bool buffer_is_flush (struct cache_entry *buffer);
//void buffer_set_flush (struct cache_entry *buffer, bool flag);
bool buffer_is_wait (struct cache_entry *buffer);
void buffer_set_wait (struct cache_entry *buffer, bool flag);

#endif /* filesys/cache.h */
