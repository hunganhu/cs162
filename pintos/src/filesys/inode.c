#include "filesys/inode.h"
#include <list.h>
#include <stdio.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Identifies an inode. */
/** ASCII value of 'INODE' */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //block_sector_t start;   /* First data sector. */
    off_t length;             /* File size in bytes. */
    unsigned magic;           /* Magic number. */
    unsigned is_dir;          /* is the inode a directory 0:false, others:true*/
    block_sector_t block[BLOCKS_NUM];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* On-disk indirect segment for an inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_indirect
{
  block_sector_t block[BLOCK_SLOTS];
};

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock lock_inode;             /* lock hold when modify */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  struct inode_disk *inode_block;
  struct inode_indirect *indirect, *dbl_indirect;
  block_sector_t indirect_idx, dbl_indirect_idx;
  block_sector_t sector;

  block_sector_t pos_sector = pos / BLOCK_SECTOR_SIZE;
  inode_block = calloc (1, sizeof (*inode_block));
  cache_block_read (fs_device, inode->sector, inode_block);
  //inode_block = &inode->data;
  
  if (pos_sector < INDIRECT_BEGIN) {
    sector = inode_block->block[pos_sector];
    if (sector == 0) {
      inode_block->block[pos_sector] = inode_alloc_zeros (&sector);
      cache_block_write (fs_device, inode->sector, inode_block);
    }
  } else if (pos_sector < DBL_INDIRECT_BEGIN) {
    sector = inode_block->block[INDIRECT_BLK];
    if (sector != BLOCK_ERROR) {
      indirect = calloc (1, sizeof (*indirect));
      cache_block_read (fs_device, sector, indirect);
      sector = indirect->block[pos_sector - INDIRECT_BEGIN];
      if (sector == 0) {
	indirect->block[pos_sector-DIRECT_BLK_LEN] = inode_alloc_zeros(&sector);
	cache_block_write (fs_device, inode_block->block[INDIRECT_BLK],
			   indirect);
      }
      free (indirect);
    }
  } else if (pos_sector < MAX_FILE_SECTOR) {
    indirect_idx = (pos_sector - DBL_INDIRECT_BEGIN) / BLOCK_SLOTS;
    dbl_indirect_idx = (pos_sector - DBL_INDIRECT_BEGIN) % BLOCK_SLOTS;
    sector = inode_block->block[DBL_INDIRECT_BLK];
    if (sector != BLOCK_ERROR) {
      indirect = calloc (1, sizeof (*indirect));
      cache_block_read (fs_device, sector, indirect);
      sector = indirect->block[indirect_idx];
      if (sector != BLOCK_ERROR) { 
	dbl_indirect = calloc (1, sizeof (*dbl_indirect));
	cache_block_read (fs_device, sector, dbl_indirect);
	sector = dbl_indirect->block[dbl_indirect_idx];
	if (sector == 0) {
	  dbl_indirect->block[dbl_indirect_idx] = inode_alloc_zeros (&sector);
	  cache_block_write (fs_device, indirect->block[indirect_idx],
			     dbl_indirect);
	}
	free (dbl_indirect);
      }
      free (indirect);
    }
  }
  free (inode_block);
  return sector;
}
/*Allocate a free block initialed to zeros, -1 if no free block found */
block_sector_t inode_alloc_zeros (block_sector_t *sector)
{
  if (free_map_allocate (1, sector)) {
    static char zeros[BLOCK_SECTOR_SIZE];
    cache_block_write (fs_device, *sector, zeros);
    return *sector;
  }
  return -1;
}


bool inode_expand_sector (struct inode *inode, block_sector_t pos_sector) 
{
  ASSERT (inode != NULL);

  struct inode_disk *inode_block;
  struct inode_indirect indirect, dbl_indirect;
  block_sector_t indirect_idx, dbl_indirect_idx, i;
  bool success = false;

  //  block_sector_t pos_sector = pos / BLOCK_SECTOR_SIZE;
  inode_block = &inode->data;
  
  if (pos_sector < INDIRECT_BEGIN) {
    if (inode_block->block[pos_sector] == BLOCK_ERROR) {
      inode_block->block[pos_sector] = 0;
    }
    cache_block_write (fs_device, inode->sector, inode_block);
    success = true;

  } else if (pos_sector < DBL_INDIRECT_BEGIN) {
    if (inode_block->block[INDIRECT_BLK] != BLOCK_ERROR) {
      cache_block_read (fs_device, inode_block->block[INDIRECT_BLK],
			&indirect);
     } else {  // allocate a new indirect segment
      if (free_map_allocate (1, &inode_block->block[INDIRECT_BLK])) {
	for (i = 0; i < BLOCK_SLOTS; i++)
	  indirect.block[i] = BLOCK_ERROR;
      } else {
	return false;
      }
      cache_block_write (fs_device, inode->sector, inode_block);
    }
 
    if (indirect.block[pos_sector - DIRECT_BLK_LEN] == BLOCK_ERROR) {
	indirect.block[pos_sector - DIRECT_BLK_LEN] = 0;
    }
    cache_block_write (fs_device, inode_block->block[INDIRECT_BLK],
		       &indirect);
    success = true;

  } else if (pos_sector < MAX_FILE_SECTOR) {
    indirect_idx = (pos_sector - DBL_INDIRECT_BEGIN) / BLOCK_SLOTS;
    dbl_indirect_idx = (pos_sector - DBL_INDIRECT_BEGIN) % BLOCK_SLOTS;

    if (inode_block->block[DBL_INDIRECT_BLK] != BLOCK_ERROR) {
      cache_block_read (fs_device, inode_block->block[DBL_INDIRECT_BLK],
			&indirect);
    } else { // allocate a new indirect segment
      if (free_map_allocate (1, &inode_block->block[DBL_INDIRECT_BLK])) {
	for (i = 0; i < BLOCK_SLOTS; i++)
	  indirect.block[i] = BLOCK_ERROR;
      } else {
	return false;
      }
      cache_block_write (fs_device, inode->sector, inode_block);
    }

    if (indirect.block[indirect_idx] != BLOCK_ERROR) { 
      cache_block_read (fs_device, indirect.block[indirect_idx],
			&dbl_indirect);
    } else { // allocate a new double indirect segment
	if (free_map_allocate (1, &indirect.block[indirect_idx])) {
	  for (i = 0; i < BLOCK_SLOTS; i++)
	    dbl_indirect.block[i] = BLOCK_ERROR;
	} else {
	  return false;
	}
	cache_block_write (fs_device, inode_block->block[DBL_INDIRECT_BLK],
		       &indirect);
    }

    if (dbl_indirect.block[dbl_indirect_idx] == BLOCK_ERROR) {
      dbl_indirect.block[dbl_indirect_idx] = 0;
    }
    cache_block_write (fs_device, indirect.block[indirect_idx],
		       &dbl_indirect);
    success =  true;
  }
  return success;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  block_sector_t  i;
  struct inode *inode = NULL;
  
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors (length);
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->is_dir = (is_dir? 1 : 0);
    for (i = 0; i < BLOCKS_NUM; i++)       //initial all blocks to -1
      disk_inode->block[i] = -1;
    
    cache_block_write (fs_device, sector, disk_inode);
    free (disk_inode);
    if (sectors > 0) {
      inode = inode_open (sector);
      for (i = 0; i < sectors; i++)
	if (!inode_expand_sector (inode, i)) {
	  return false;
	}
      //cache_block_write (fs_device, sector, &inode->data);
      inode_close (inode);
    }
    success = true;
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock_inode);
  //block_read (fs_device, inode->sector, &inode->data);
  cache_block_read (fs_device, inode->sector, &inode->data);
  IDEBUG ("inode open: %p(%d),sector=%d.\n", inode, inode->open_cnt, inode->sector);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  IDEBUG ("inode reopen: %p(%d),sector=%d.\n", inode, inode->open_cnt, inode->sector);
  return inode;
}
/* Return inode for a path. Return NULL if path_name is invalid.
   Copy the last token not ended with '/' to file_name.
*/
struct inode *
inode_open_path (const char *path_name, char *file_name)
{
  char *save_ptr, *token;  // declare variables for strtok_r()
  char *delimiters = "/\\";
  char *path = malloc (strlen(path_name) + 1); //length include '\0'
  struct inode *inode = NULL;
  struct dir *working_dir;
  bool error = false;
  struct thread *t = thread_current ();

  *file_name = '\0';      // initial an empty filename
  if (*path_name =='\0')  // empty string
    return inode;

  if (!strcmp (path_name, "/")) { // root directory need special handling 
    *file_name = '\0';
    inode = inode_open (ROOT_DIR_SECTOR);
    return inode;
  }

  strlcpy (path, path_name, strlen(path_name) + 1);
  if (*path == '/')
    working_dir = dir_open_root ();
  else {
    working_dir = dir_reopen (t->cur_dir);
  }

  for (token = strtok_r (path, delimiters, &save_ptr);
       token != NULL;
       token = strtok_r (NULL, delimiters, &save_ptr)) {

    if (*save_ptr == '\0') { // token is the last one
      strlcpy (file_name, token, strlen(token) + 1);
    } else { // there are tokens left
      //check token is in working_dir
      if (!dir_lookup (working_dir, token, &inode)) {
	error = true;
	break;
      } else {
	if (inode_is_dir (inode)) {  // inode must be a directory
	  dir_close (working_dir);
	  working_dir = dir_open (inode);
	}
	else { //inode is not directory
	  error = true;
	  break;
	}
      }
    } 
  }
  if (!error)
    dir_lookup (working_dir, ".", &inode);  //get inode of working directory

  dir_close (working_dir);
  free (path);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  // write all dirty pages to disk
  inode_flush (inode);
  /* Release resources if this was the last opener. */
  IDEBUG ("inode before close: %p(%d),sector=%d.\n", inode, inode->open_cnt, inode->sector);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed) 
	inode_release (inode);

      free (inode); 
    }
}
/* free inode's disk blocks  */
void inode_release(struct inode *inode) 
{
  ASSERT (inode != NULL);

  struct inode_indirect indirect, dbl_indirect;
  block_sector_t indirect_idx = 0;
  block_sector_t dbl_indirect_idx = 0;
  block_sector_t direct_min, indirect_min, dbl_indirect_min, i;
  block_sector_t sectors = bytes_to_sectors (inode_length (inode));
  struct inode_disk *inode_data = &inode->data;

  // free inode itself
  free_map_release (inode->sector, 1);

  // start to free inode data
  direct_min = sectors < INDIRECT_BEGIN ? sectors : INDIRECT_BEGIN;
  for (i = DIRECT_BEGIN; i < direct_min; i++) {
    if (inode_data->block[i] != 0)
      free_map_release (inode_data->block[i], 1);
  }

  indirect_min = sectors < DBL_INDIRECT_BEGIN ? sectors : DBL_INDIRECT_BEGIN;
  for (i = INDIRECT_BEGIN; i < indirect_min; i++) {
    if (i ==  INDIRECT_BLK && inode_data->block[i] != BLOCK_ERROR)
      cache_block_read (fs_device, inode_data->block[INDIRECT_BLK],
			&indirect);
      
    if (indirect.block[i] != 0)
      free_map_release (indirect.block[i], 1);
  }

  dbl_indirect_min = sectors < MAX_FILE_SECTOR ? sectors : MAX_FILE_SECTOR;
  for (i = DBL_INDIRECT_BEGIN; i < dbl_indirect_min; i++) {
    indirect_idx = (i - DBL_INDIRECT_BEGIN) / BLOCK_SLOTS;
    dbl_indirect_idx = (i - DBL_INDIRECT_BEGIN) % BLOCK_SLOTS;

    // read the indirect segment
    if (i ==  DBL_INDIRECT_BLK && inode_data->block[i] != BLOCK_ERROR)
      cache_block_read (fs_device, inode_data->block[DBL_INDIRECT_BLK],
			&indirect);
    // read the double indirect segment
    if (indirect_idx == 0 && indirect.block[indirect_idx] != BLOCK_ERROR)
      cache_block_read (fs_device, indirect.block[indirect_idx], &dbl_indirect);

    // free the sector
    if (dbl_indirect.block[dbl_indirect_idx] != 0)
      free_map_release (dbl_indirect.block[dbl_indirect_idx], 1);

    // if it is the last sector in the double indirect segement, free the
    // double indirect segement.
    if (dbl_indirect_idx == (BLOCK_SLOTS - 1))
      free_map_release (indirect.block[indirect_idx], 1);
  }
  // free the last double indirect segment and indirect segment
  if (i == dbl_indirect_min) {
      free_map_release (indirect.block[indirect_idx], 1);
      free_map_release (inode_data->block[DBL_INDIRECT_BLK], 1);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == BLOCK_ERROR)
	break;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          //block_read (fs_device, sector_idx, buffer + bytes_read);
          cache_block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          //block_read (fs_device, sector_idx, bounce);
          cache_block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Expand SIZE bytes of zeros into INODE, starting at OFFSET.
   Returns the number of bytes actually expanded and change inode length 
   to the new value. */
off_t
inode_expand_zero (struct inode *inode, off_t size, off_t offset) 
{
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  block_sector_t sector_idx;
  block_sector_t pos_sector;

  if (inode->deny_write_cnt)
    return 0;

  inode->data.length = offset + size;
  cache_block_write (fs_device, inode->sector, &inode->data);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      if (sector_idx == BLOCK_ERROR) {
	pos_sector = offset /  BLOCK_SECTOR_SIZE;
	// mark the sector with zero sector
	if (inode_expand_sector (inode, pos_sector))
	  sector_idx = 0;
	else
	  break;
      } 
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Mark the sector with zero sector, which is done in the begining
	     of the while loop. */
        }
      else 
        {
	  // allocate a zeros sector
	  if (sector_idx == 0)
	    sector_idx = byte_to_sector (inode, offset);
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memset (bounce + sector_ofs, 0, chunk_size);
          cache_block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // set the length of inode to new offset
  inode->data.length = offset;
  free (bounce);

  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  off_t inode_size = inode_length (inode);
  block_sector_t sector_idx;
  block_sector_t pos_sector;

  if (inode->deny_write_cnt)
    return 0;

  if (offset > inode_size) {
    inode_lock (inode);
    inode_expand_zero (inode, offset + size - inode_size, inode_size);
    inode_unlock (inode);
  }

  if ((offset + size) > inode_length (inode)) {
    inode_lock (inode);
    inode->data.length = offset + size;
    cache_block_write (fs_device, inode->sector, &inode->data);
    inode_unlock (inode);
  }
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      if (sector_idx == BLOCK_ERROR) {
	pos_sector = offset / BLOCK_SECTOR_SIZE;
	if (inode_expand_sector (inode, pos_sector))
	  sector_idx = byte_to_sector (inode, offset);
	else
	  break;
	//re-read the inode_disk
	cache_block_read (fs_device, inode->sector, &inode->data);
      }

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          //block_write (fs_device, sector_idx, buffer + bytes_written);
          cache_block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) { 
            //block_read (fs_device, sector_idx, bounce);
            cache_block_read (fs_device, sector_idx, bounce);
	  }
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          //block_write (fs_device, sector_idx, bounce);
          cache_block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // set the length of inode to new offset
  inode_lock (inode);
  //  inode->data.length = offset;
  cache_block_write (fs_device, inode->sector, &inode->data);
  inode_unlock (inode);
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Returns the open count of INODE. */
int
inode_open_cnt (const struct inode *inode)
{
  return inode->open_cnt;
}

/* Returns true if inode is a directory */
bool inode_is_dir (const struct inode *inode)
{
  return inode != NULL && inode->data.is_dir != 0 ? true : false;
}
/* flush all dirty sectors of the inode to disk */
void
inode_flush (struct inode *inode) 
{
  block_sector_t sectors;   // number of sectors of an inode
  block_sector_t sector;    // the sector number of the disk block
  block_sector_t i;         // index of a loop 
  struct cache_entry *buffer; //buffer cache

  // flush inode
  buffer = cache_lookup (inode->sector);
  if (buffer != NULL) {
    if (buffer_is_delayed(buffer)) {
      acquire_shared (&buffer->lock_shared);	
      cache_flush_buffer (buffer);
      release_shared (&buffer->lock_shared);
    }
  }

  // flush inode data
  sectors = bytes_to_sectors (inode->data.length);
  for (i = 0; i < sectors; i++) {
    sector = byte_to_sector (inode, i * BLOCK_SECTOR_SIZE);
    buffer = cache_lookup (sector);
    if (buffer != NULL) {
      if (buffer_is_delayed(buffer)) {
	acquire_shared (&buffer->lock_shared);
	cache_flush_buffer (buffer);
	release_shared (&buffer->lock_shared);
      }
    }
  } 
}

void inode_lock (struct inode *inode)
{
  lock_acquire (&inode->lock_inode);
}
void inode_unlock (struct inode *inode)
{
  lock_release (&inode->lock_inode);
}

/* Devide the given NAME into two parts, the path and the last part, either 
   a file or a directory. The PATH and LAST should have enough space to held
   the return values. 
*/
bool
path_parse (const char *name, char *path, char *last)
{
  char *save_ptr, *token, **tokenv;  // declare variables for strtok_r()
  char *delimiters = "/\\";
  int tokenc, i;
  char *name_str = malloc (strlen(name) + 1); //length include '\0'
  bool success = false;

 /** get the number of tokens*/
  strlcpy (name_str, name, strlen(name) + 1);
  for (token = strtok_r (name_str, delimiters, &save_ptr), tokenc = 0;
       token != NULL;
       token = strtok_r (NULL, delimiters, &save_ptr))
    tokenc++;

  if (tokenc == 0)
    return success;

  /** get the tokens */
  tokenv = malloc (tokenc * sizeof (char *));
  strlcpy (name_str, name, strlen(name) + 1);
  for (token = strtok_r (name_str, delimiters, &save_ptr), i = 0;
       token != NULL;
       token = strtok_r (NULL, delimiters, &save_ptr))
    tokenv[i++] = token;

  if (path != NULL && last != NULL) {
    if (*name == '/')
      strlcpy (path, "/", 1);
    else
      *path = '\0';

    if (tokenc < 2) {
      strlcpy (last, tokenv[tokenc - 1], strlen (tokenv[tokenc - 1]) + 1);
    } else {
      strlcpy (last, tokenv[tokenc - 1], strlen (tokenv[tokenc - 1]) + 1);
      strlcat (path, tokenv[0], strlen (tokenv[0]));
      for (i == 1; i < tokenc - 1; i++) {
	strlcat (path, "/", 1);
	strlcat (path, tokenv[i], strlen (tokenv[i]));
      }
    }
    success = true;
  }
  
  free (name_str);
  free (tokenv);
  return success;

}
