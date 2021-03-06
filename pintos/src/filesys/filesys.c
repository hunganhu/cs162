#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();    /** initial an empty inode list */  
  free_map_init (); /** create block free map in memory*/

  if (format) 
    do_format ();   /** writ block bitmap to a file and create root dir */

  free_map_open (); /** restore block bitmap from free_map file */
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  //printf ("flush cache.\n");
  cache_flush_cache ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  char *file_name = malloc (strlen (name) + 1);
  struct inode *inode;
  bool success = false;

  inode = inode_open_path (name, file_name);
  if (inode == NULL && *file_name == '\0') {
    free (file_name);
    return success;
  }

  struct dir *dir = dir_open (inode);
  success = (dir != NULL
	     && free_map_allocate (1, &inode_sector)
	     && inode_create (inode_sector, initial_size, false)
	     && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free (file_name);

  return success;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_mkdir (const char *name) 
{
  block_sector_t inode_sector = 0;
  char dir_name[NAME_MAX + 1];
  struct inode *inode;
  bool success = false;

  inode = inode_open_path (name, dir_name);
  if (inode == NULL && *dir_name == '\0')
    return success;

  struct dir *dir = dir_open (inode);
  success = (dir != NULL
	     && free_map_allocate (1, &inode_sector)
	     && dir_create (inode_sector, 2)
	     && dir_add (dir, dir_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  if (success) {
    struct dir *subdir = dir_open (inode_open (inode_sector));
    success = (subdir != NULL &&
	       dir_add (subdir, ".", inode_sector) &&
	       dir_add (subdir, "..", inode_get_inumber(dir_get_inode(dir)))
	       );
    dir_close (subdir);
  }
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char *file_name = malloc (strlen (name) + 1);
  struct inode *inode_path = inode_open_path (name, file_name);
  struct dir *working_dir;
  struct inode *inode = NULL;
  struct file *file_ptr = NULL;

  if (inode_path != NULL) {
    working_dir = dir_open (inode_path);
    if (*file_name !='\0' && strcmp (file_name, ".")
	&& strcmp (file_name, "..")) { // file_name is not "." or ".."
      dir_lookup (working_dir, file_name, &inode);
      if (inode != NULL) {
	file_ptr = file_open (inode);
      }
    }
    else { //no file name, then it is a directory
      if (!strcmp (file_name, "..")) {
	dir_lookup (working_dir, "..", &inode); //parent as inode
      } else {
	dir_lookup (working_dir, ".", &inode); // current as inode
      }
      if (inode != NULL) {
	file_ptr = file_open (inode);
      }
    }
    dir_close (working_dir);
  }

  free (file_name);
  return file_ptr;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool success = false;

  // root directory cannot remove
  if (!strcmp (name, "/"))
    return success;

  struct dir *dir;
  char *file_name = malloc (strlen (name) + 1);
  struct inode *inode_path = inode_open_path (name, file_name);

  if (inode_path != NULL) {
    dir = dir_open (inode_path);
    if (*file_name !='\0') {
      if (dir_remove (dir, file_name)) {
	success = true;
      }
    } else {  // no file name, ie. current directory
      if (dir_remove (dir, ".")) {
	success = true;
      }
    }
    dir_close (dir);
  }
  free (file_name);
  return success;
}

/* Formats the file system. */
/** writ block bitmap to free bitmap file and create root dir */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();    /** write block free map to a file */
  if (!dir_create (ROOT_DIR_SECTOR, 16)) /** create root dir with 16 entries */
    PANIC ("root directory creation failed");

  free_map_close ();
  printf ("done.\n");
}
