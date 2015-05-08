#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

#define INODE_TRACE false
#define IDEBUG if (INODE_TRACE) printf

#define BLOCKS_NUM 125
#define DIRECT_BEGIN  0
#define INDIRECT_BEGIN 123
#define DBL_INDIRECT_BEGIN (DIRECT_BLK_LEN + BLOCK_SLOTS)
#define DIRECT_BLK_LEN (INDIRECT_BEGIN - DIRECT_BEGIN)

#define INDIRECT_BLK 123
#define DBL_INDIRECT_BLK 124
#define BLOCK_SLOTS (BLOCK_SECTOR_SIZE / sizeof (block_sector_t))
#define MAX_FILE_SECTOR (DIRECT_BLK_LEN + BLOCK_SLOTS+ BLOCK_SLOTS*BLOCK_SLOTS)
#define MAX_FILE_LEN (MAX_FILE_SECTOR * BLOCK_SECTOR_SIZE)
#define BLOCK_ERROR ((block_sector_t) -1) 

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool is_dir);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool inode_is_dir (const struct inode *inode);
int inode_open_cnt (const struct inode *inode);
void inode_flush (struct inode *inode);
block_sector_t inode_alloc_zeros (block_sector_t *sector);
bool inode_expand_sector (struct inode *inode, block_sector_t pos_sector);
off_t inode_expand_zero (struct inode *inode, off_t size, off_t offset);
void inode_release (struct inode *inode);
struct inode *inode_open_path (const char *path_name, char *file_name);
void inode_lock (struct inode *inode);
void inode_unlock (struct inode *inode);

#endif /* filesys/inode.h */
