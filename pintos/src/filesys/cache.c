/* pintos/src/vm/page.c
*/
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/cache.h"

struct hash buffer_cache;     /* hash table maintaining the buffer */
struct list list_lru;         /* free list preserves the LRU order */
struct semaphore sema_lru;    /* event to indicate a free buffer is available */
struct lock lock_buffercache; /* lock when accessing buffer cache hash table */

//static struct cache_entry *breada (struct block *block, block_sector_t sector,
//				   block_sector_t sector_next);

/* cache buffer initial do:
   1. create hash table
   2. create free list
   3. initial semaphore sema_lru
   4. allocate kernel pages of 64 block size and assign to free list
   5. initial a thread to flush cache buffer periodically
*/
void cache_init (void)
{
  struct cache_entry *buffer;
  int i;

  hash_init (&buffer_cache, cache_hash_value, cache_hash_less, NULL);
  list_init (&list_lru);
  sema_init (&sema_lru, 0);
  lock_init (&lock_buffercache);

  for (i = 0; i < BUFFER_CACHE_SIZE; i++) {
    buffer = malloc (sizeof (struct cache_entry));
    buffer->seq = i;
    buffer->sector = (block_sector_t) -1;
    memset (buffer->data, 0, BLOCK_SECTOR_SIZE);
    buffer->status = 0;
    sema_init (&buffer->sema_buf, 1);
    init_shared (&buffer->lock_shared);
    list_push_back (&list_lru, &buffer->list_elem);
  }
  /*Initial a thread to flush cache buffer every one second. */
  /*TODO: after adding this thread, cases with concurrent processes are failed*/
  thread_create ("CACHE_FLUSH", PRI_DEFAULT, cache_flush_cache, NULL);
}

/* Return the hash value of the hash element ce on the value of sector. */
unsigned
cache_hash_value (const struct hash_elem *ce, void *aux UNUSED)
{
  const struct cache_entry *c = hash_entry (ce, struct cache_entry, hash_elem);
  return hash_bytes (&c->sector, sizeof c->sector);
}

/* Returns true if cache entry a precedes cache entry b on the value of sector.
 */
bool
cache_hash_less (const struct hash_elem *a, const struct hash_elem *b,
		void *aux UNUSED)
{
  const struct cache_entry *ca = hash_entry (a, struct cache_entry, hash_elem);
  const struct cache_entry *cb = hash_entry (b, struct cache_entry, hash_elem);
  return ca->sector < cb->sector;
}

/*
  Return a cache entry of the buffer cache for the specific file block.
  Return NULL if not found.
 */
struct cache_entry *
cache_lookup (block_sector_t sector)
{
  struct cache_entry buffer;
  struct hash_elem *e;

  buffer.sector = sector;
  lock_acquire (&lock_buffercache);
  e = hash_find (&buffer_cache, &buffer.hash_elem);
  lock_release (&lock_buffercache);

  return e != NULL ? hash_entry (e, struct cache_entry, hash_elem) : NULL;
}
/* Scenarios for retrieval of a buffer (see chapter 3 Buffer Cache of 
   the Design of the Unix Operating System, Maurice J. Bach)
   1. The kernel find the block on its hash queue, and its buffer is free
   2. The kernel cannot find the block on the hash queue, so it allocates a
      buffer from the free list.
   3. The kernel cannot find the block on the hash queue and, in attemptin to 
      allocate a buffer from the free list, finds a buffer on the free list
      that has been marked "delayed write". The kernel must write the
      "delayed write" buffer to disk and allocated another buffer.
   4. The kernel cannot find the block on the hash queue, and the free list of
      buffer is empty.
   5. The kernel finds the block on the hash queue, but its buffer is currently
      busy.
*/
struct cache_entry *cache_get_buffer (block_sector_t sector)
{
  bool found = false;
  struct cache_entry *buffer;
  enum intr_level old_level;

  while (!found) {
    buffer = cache_lookup (sector);
    if (buffer != NULL) {
      //scenario 5, wait event the buffer becomes free 
      acquire_exclusive (&buffer->lock_shared);
      if (buffer->sector != sector) { // reckeck the sector after wait up
	release_exclusive (&buffer->lock_shared);
	continue;
      }
      //disable interrupt while manuplating the free list
      if (sector > ROOT_DIR_SECTOR) {
	old_level = intr_disable (); //scenario 1
	list_remove (&buffer->list_elem);
	intr_set_level (old_level);
      }

      found = true;
    } else { // block not on hash queue
      if (list_empty (&list_lru)) {        //scenario 4
	sema_down (&sema_lru);    //wait event any buffer becomes free    
	continue;
      }
      //disable interrupt while manuplating the free list
      old_level = intr_disable ();
      buffer = list_entry(list_pop_front(&list_lru), struct cache_entry,
			  list_elem);
      intr_set_level (old_level);
      if (buffer == NULL)
	continue;
      else {
	if (buffer_is_delayed (buffer)) { //scenario 3
	  // asynchronous write buffer to disk
	  acquire_shared (&buffer->lock_shared);
	  cache_flush_buffer (buffer);
	  release_shared (&buffer->lock_shared);
	}
	acquire_exclusive (&buffer->lock_shared);
      }
      //scenarion 2: found a free buffer
      lock_acquire (&lock_buffercache);
      hash_delete (&buffer_cache, &buffer->hash_elem);
      lock_release (&lock_buffercache);

      found = true;
    }
  }
  return buffer;
}

/*
  Release the cache entry when kernel finishing using the buffer
*/
void cache_release (struct cache_entry *buffer)
{
  enum intr_level old_level;
  
  // FREE_MAP_SECTOR and ROOT_DIR_SECTOR are pinned in buffer cache
  if (buffer->sector > ROOT_DIR_SECTOR) {
    //disable interrupt while manuplating the free list
    old_level = intr_disable ();
    list_push_back (&list_lru, &buffer->list_elem);
    intr_set_level (old_level);
    sema_up (&sema_lru);
  }
  release_exclusive (&buffer->lock_shared);
}

void cache_flush_buffer (struct cache_entry *buffer)
{
  //initiate disk write
  block_write (fs_device, buffer->sector, buffer->data);
  buffer_set_delayed(buffer, false);
  CDEBUG ("cache-flush: buffer[%d] to %s[%d].\n", buffer->seq,
	  block_type_name(block_type(fs_device)), buffer->sector);
}

void cache_flush_cache (void *AUX UNUSED)
{
  struct cache_entry *buffer;
  struct hash_iterator i;

  for (;;) {
    timer_sleep (TIMER_FREQ / 5); // sleep one second
    CDEBUG ("***** wake up after 0.2 second.\n");
    lock_acquire (&lock_buffercache);
    hash_first (&i, &buffer_cache);
    while (hash_next (&i)) {
      buffer = hash_entry (hash_cur (&i), struct cache_entry, hash_elem);
      if (buffer != NULL && buffer_is_delayed(buffer)) {
	acquire_shared (&buffer->lock_shared);
	cache_flush_buffer (buffer);
	release_shared (&buffer->lock_shared);
	CDEBUG ("daemon-flush: buffer[%d] to %s[%d].\n", buffer->seq,
		block_type_name(block_type(fs_device)), buffer->sector);
      }
    }
    lock_release (&lock_buffercache);
  }
}

/*
//block read ahead

static struct cache_entry 
*breada (struct block *block, block_sector_t sector, block_sector_t sector_next)
{
  struct cache_entry *buffer1, *buffer2;

  buffer1 = cache_lookup (sector);
  if (buffer1 == NULL) {
    buffer1 = bread (block, sector);
  }
  buffer2 = cache_lookup (sector_next);
  if (buffer2 == NULL) {
    buffer2 = bread (block, sector_next);
    cache_release (buffer2);
  }
  return buffer1; 
}
*/

void cache_block_read (struct block *block, block_sector_t sector, void *data)
{
  struct cache_entry *buffer;
  enum intr_level old_level;

  buffer = cache_get_buffer(sector);
  if (buffer->sector != sector) {
    buffer->sector = sector;
    lock_acquire (&lock_buffercache);
    hash_insert (&buffer_cache, &buffer->hash_elem);
    lock_release (&lock_buffercache);
    //initiate disk read
    block_read (block, sector, buffer->data);
  }  
  /* Before copying data from cache to memory, change the lock to shared mode
     to allow parallelism
  */
  old_level = intr_disable ();
  cache_release (buffer);
  acquire_shared (&buffer->lock_shared);
  intr_set_level (old_level);

  memcpy (data, buffer->data, BLOCK_SECTOR_SIZE);
  release_shared (&buffer->lock_shared);

  CDEBUG ("cache-read: buffer[%d] from %s[%d].\n", buffer->seq,
  	  block_type_name(block_type(block)), sector);
}

void cache_block_write (struct block *block UNUSED, block_sector_t sector,
			const void *data)
{
  struct cache_entry *buffer;
  buffer = cache_get_buffer (sector);
  buffer->sector = sector;
  CDEBUG ("cache-write: buffer[%d] to %s[%d].\n", buffer->seq, 
  	  block_type_name(block_type(block)), sector);
  memcpy (&buffer->data, data, BLOCK_SECTOR_SIZE);

  if (buffer != NULL && buffer->sector != (block_sector_t) -1 ) {
    lock_acquire (&lock_buffercache);
    hash_insert (&buffer_cache, &buffer->hash_elem);
    lock_release (&lock_buffercache);
    buffer_set_delayed (buffer, true);
    cache_release (buffer);
  }
}

bool buffer_is_delayed (struct cache_entry *buffer)
{
  return buffer->status & CACHE_DELAYED;
}  

void buffer_set_delayed (struct cache_entry *buffer, bool flag)
{
  if (flag)
    buffer->status |=  CACHE_DELAYED;
  else
    buffer->status &= ~CACHE_DELAYED;
}  

bool buffer_is_busy (struct cache_entry *buffer)
{
  return buffer->status & CACHE_BUSY;
}  
void buffer_set_busy (struct cache_entry *buffer, bool flag)
{
  if (flag)
    buffer->status |=  CACHE_BUSY;
  else
    buffer->status &= ~CACHE_BUSY;
}  

void init_shared (struct shared_lock *s)
{
  s->i = 0;
  lock_init (&s->lock);
  cond_init (&s->cond);
}

/* Acquire lock in shared mode */
void 
acquire_shared (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  while (s->i < 0)
  {
    cond_wait (&s->cond, &s->lock);
  }
  s->i ++;
  lock_release (&s->lock);
}

/* Acquire lock in exlusive mode */
void 
acquire_exclusive (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  while (s->i)
  {
    cond_wait (&s->cond, &s->lock);
  }
  s->i = -1;
  lock_release (&s->lock);
}

/* Release lock in shared mode */
void 
release_shared (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  if (!--s->i) 
    cond_signal (&s->cond, &s->lock);
  lock_release (&s->lock);
}

/* Release lock in exclusive mode */
void
release_exclusive (struct shared_lock *s)
{
  lock_acquire (&s->lock);
  s->i = 0;
  cond_broadcast (&s->cond, &s->lock);
  lock_release (&s->lock);
}

