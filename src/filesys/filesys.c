#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* For Project 4 */
bool filesystem_shutdown;

// Util Functions
void split(const char* path, char* dir, char* base) {
	size_t len = strlen(path);
	const char *last_slash = strrchr(path, '/');
	if(last_slash) {
		// With Directory
		// Set dir the path, and the base last
		strlcpy(dir, path, last_slash - path + 1);
		strlcpy(base, last_slash + 1, len - (last_slash - path));
	}
	else {
		// Only File Name
		// Set dir ".", base total path.
		strlcpy(dir, ".", len + 1);
		strlcpy(base, path, len + 1);
	}
}

struct dir* try_dir_open(char* dir) {
	struct dir* cur = NULL;
	struct thread* cur_thread = thread_current();

	if(cur_thread->cwd) {
		cur = dir_reopen(cur_thread->cwd);
	}
	size_t i = 0;
	if(dir[0] == '/' || dir[0] == '\0') {
		cur = dir_open_root();
		while(dir[i] == '/') i++;
	}
	else if (cur == NULL) {
		cur = dir_open_root();
	}
	size_t l = strlen(dir);
	while(i < l) {
		size_t j = i;
		while(j < l && dir[j] != '/') j++;
		dir[j] = '\0';

		Inode* inode = NULL;
		if(!dir_lookup(cur, dir + i, &inode)) {
			dir_close(cur);
			return NULL;
		}
		dir_close(cur);
		if(!inode_is_dir(inode)) {
			return NULL;
		}
		cur = dir_open(inode);
		i = j + 1;
	}
	return cur;
}

bool open_pd_and_get_basename(const char* name, struct dir** dir, char** basename) {
	size_t size = strlen(name) + 1;
	char* parent_dir_path = (char*)malloc(size);
	*basename = (char*)malloc(size);

	split(name, parent_dir_path, *basename);
	size_t basename_size = strlen(*basename);
	if(basename_size == 0) {
		free(parent_dir_path);
		free(*basename);
		return false;
	}
	struct dir* parent_dir = try_dir_open(parent_dir_path);
	free(parent_dir_path);
	if(parent_dir == NULL) {
		free(*basename);
		return false;
	}
	*dir = parent_dir;
	return true;
}

bool filesys_open_f_or_d(const char* name, struct file **file, struct dir **dir) {
	// Special Case: Input "/" means root directory.
	if(name[0] == '/' && name[1] == '\0') {
		*file = NULL;
		*dir = dir_open_root();
		return true;
	}
	*file = NULL;
	*dir = NULL;
	struct dir* parent_dir;
	char* basename;

	if(!open_pd_and_get_basename(name, &parent_dir, &basename)) {
		return false;
	}
	Inode* inode = NULL;
	dir_lookup(parent_dir, basename, &inode);
	dir_close(parent_dir);
	if(inode == NULL) {	
		return false;
	}
	if(inode_is_dir(inode)) {
		*dir = dir_open(inode);
		*file = NULL;
	}
	else {
		*file = file_open(inode);
		*dir = NULL;
	}
	//free(basename);
	return true;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
	fs_device = block_get_role(BLOCK_FILESYS);
	if (fs_device == NULL)
		PANIC("No file system device found, can't initialize file system.");

	// Project 4
	cache_init();

	inode_init();
	free_map_init();

	if (format)
		do_format();

	free_map_open();
	filesystem_shutdown = false;
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
	write_back_all_cache();
	free_map_close();
	filesystem_shutdown = true;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create(const char* name, off_t initial_size)
{
	// Project 4
	struct dir* parent_dir;
	char* basename;
	if(!open_pd_and_get_basename(name, &parent_dir, &basename)) {
		return false;
	}
	// Create Directory
	block_sector_t inode_sector = 0;
	bool success = (parent_dir != NULL
		&& free_map_allocate(1, &inode_sector)
		&& inode_create(inode_sector, initial_size)
		&& dir_add(parent_dir, basename, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release(inode_sector, 1);
	dir_close(parent_dir);

	return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {
	struct dir* parent_dir;
	char* basename;
	if(!open_pd_and_get_basename(name, &parent_dir, &basename)) {
		return NULL;
	}
	Inode* inode = NULL;
	dir_lookup(parent_dir, basename, &inode);
	dir_close(parent_dir);
	return file_open(inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) {
	// struct dir* dir = dir_open_root();
	// bool success = dir != NULL && dir_remove(dir, name);
	// dir_close(dir);

	// return success;
	struct dir* parent_dir;
	char* basename;
	if (!open_pd_and_get_basename(name, &parent_dir, &basename))
		return false;
		
	Inode* inode;
	dir_lookup(parent_dir, basename, &inode);

	bool success = false;

	if (!inode) {
		dir_close(parent_dir);
		return false;
	}
	if (inode_is_dir(inode)) {
		struct dir* d = dir_open(inode);
		if (dir_get_inode(d)->open_cnt == 1 && dir_is_empty(d)){ 
			success = dir_remove(parent_dir, basename);
		}
		dir_close(d);
	}
	else {
		inode_close(inode);
		success = dir_remove(parent_dir, basename);
	}

	dir_close(parent_dir);
	return success;
}

bool filesys_mkdir(const char* name) {
	struct dir* parent_dir;
	char* basename;
	if(!open_pd_and_get_basename(name, &parent_dir, &basename)) {
		return false;
	}

	// Create Directory
	Inode* inode = NULL;
	if(dir_lookup(parent_dir, basename, &inode)) {
		inode_close(inode);
		dir_close(parent_dir);
		return false;
	}
	block_sector_t inode_sector = 0;
	free_map_allocate(1, &inode_sector);
	dir_create(inode_sector, 16);
	struct dir* dir = dir_open(inode_open(inode_sector));
	dir_add(dir, ".", inode_sector);
	dir_add(dir, "..", inode_get_inumber(dir_get_inode(parent_dir)));
	dir_add(parent_dir, basename, inode_sector);

	dir_close(dir);
	dir_close(parent_dir);
	return true;
}

bool filesys_chdir(const char* name) {
	struct file* file;
	struct dir* dir;
	if(!filesys_open_f_or_d(name, &file, &dir) || dir == NULL) {
		return false;
	}
	struct thread* cur = thread_current();
	if(cur->cwd) {
		dir_close(cur->cwd);
	}
	cur->cwd = dir;
	return true;
}

/* Formats the file system. */
static void do_format(void) {
	printf("Formatting file system...");
	free_map_create();
	if (!dir_create(ROOT_DIR_SECTOR, 16))
		PANIC("root directory creation failed");
	struct dir* root = dir_open_root();
	dir_add(root, ".", ROOT_DIR_SECTOR);
	dir_add(root, "..", ROOT_DIR_SECTOR);
	dir_close(root);
	free_map_close();
	printf("done.\n");
}
