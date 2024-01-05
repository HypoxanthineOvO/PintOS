#include "filesys/inode.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define INVALID_SECTOR 0xffffffff

bool check_indirect(InodeIndirect* indirect, int position) {
	if (indirect->data[position] == 0){
		return false;
	}
	return true;
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) {
	return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
	ASSERT(inode != NULL);
	int sector_position = pos / BLOCK_SECTOR_SIZE;
	if (sector_position < DIRECT_BLOCK_NUM) {
		// Direct block
		block_sector_t sector = inode->data.direct[sector_position];
		check_sector(fs_device, sector);
		return sector;
	}

	// Indirect block
	sector_position -= DIRECT_BLOCK_NUM;
	// Location (Block_ID, Sector_ID)
	int indirect_block_ID = sector_position / INDIRECT_BLOCK_NUM,
		indirect_sector_ID = sector_position % INDIRECT_BLOCK_NUM;
	if (indirect_block_ID >= INDIRECT_BLOCK_NUM) {
		// Out of range
		return INVALID_SECTOR;
	}

	// Get double indirect block
	InodeIndirect* double_indirect = malloc(BLOCK_SECTOR_SIZE);
	cache_read(inode->data.double_indirect, double_indirect, 0, BLOCK_SECTOR_SIZE);
	if(!check_indirect(double_indirect, indirect_block_ID)) {
		// Indirect block not allocated
		free(double_indirect);
		return INVALID_SECTOR;
	}
	// Get indirect block
	InodeIndirect* indirect = malloc(BLOCK_SECTOR_SIZE);
	cache_read(double_indirect->data[indirect_block_ID], indirect, 0, BLOCK_SECTOR_SIZE);
	block_sector_t retval = INVALID_SECTOR;
	if(check_indirect(indirect, indirect_sector_ID)) {
		retval = indirect->data[indirect_sector_ID];
	}
	check_sector(fs_device, retval);
	free(double_indirect);
	free(indirect);
	return retval;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) {
	list_init(&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
	struct inode_disk* disk_inode = NULL;
	bool success = false;

	ASSERT(length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	   one sector in size, and you should fix that. */
	ASSERT(sizeof * disk_inode == BLOCK_SECTOR_SIZE);

	disk_inode = calloc(1, sizeof * disk_inode);
	if (disk_inode != NULL)
	{
		size_t sectors = bytes_to_sectors(length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		if (inode_update(disk_inode, sectors)) {
			cache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
			success = true;
		}
		free(disk_inode);
	}
	return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode*
	inode_open(block_sector_t sector)
{
	struct list_elem* e;
	struct inode* inode;

	/* Check whether this inode is already open. */
	for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
		e = list_next(e))
	{
		inode = list_entry(e, struct inode, elem);
		if (inode->sector == sector)
		{
			inode_reopen(inode);
			return inode;
		}
	}

	/* Allocate memory. */
	inode = malloc(sizeof * inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	lock_init(&inode->lock);
	list_push_front(&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	cache_read(inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
	return inode;
}

/* Reopens and returns INODE. */
struct inode*
	inode_reopen(struct inode* inode)
{
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode* inode)
{
	return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close(struct inode* inode)
{
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0)
	{
		/* Remove from inode list and release lock. */
		list_remove(&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed)
		{
			free_map_release(inode->sector, 1);
			
			// Free all blocks with garbage collection
			InodeDisk* data = &inode->data;
			for(int i = 0; i < DIRECT_BLOCK_NUM; i++) {
				if(data->direct[i]) free_map_release(data->direct[i], 1);
			}
			InodeIndirect double_indirect;
			if(data->double_indirect) {
				cache_read(data->double_indirect, &double_indirect, 0, BLOCK_SECTOR_SIZE);
				for(int i = 0; i < INDIRECT_BLOCK_NUM; i++) {
					if(double_indirect.data[i]) {
						InodeIndirect indirect;
						cache_read(double_indirect.data[i], &indirect, 0, BLOCK_SECTOR_SIZE);
						for(int j = 0; j < INDIRECT_BLOCK_NUM; j++) {
							if(indirect.data[j]) free_map_release(indirect.data[j], 1);
						}
						free_map_release(double_indirect.data[i], 1);
					}
				}
				free_map_release(data->double_indirect, 1);
			}

		}

		free(inode);
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove(struct inode* inode)
{
	ASSERT(inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset)
{
	lock_acquire(&inode->lock);
	uint8_t* buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t* bounce = NULL;

	// Check Size
	if(offset > inode->data.length) {
		lock_release(&inode->lock);
		return 0;
	}
	if(offset + size > inode->data.length) {
		size = inode->data.length - offset;
	}

	while (size > 0)
	{
		/* Disk sector to read, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	//free(bounce);

	lock_release(&inode->lock);
	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at(struct inode* inode, const void* buffer_, off_t size,
	off_t offset)
{
	const uint8_t* buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t* bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	lock_acquire(&inode->lock);

	// Check for if we need to extend inode
	if (offset + size > inode->data.length) {
		int sectors = bytes_to_sectors(offset + size);
		if (!inode_update(&inode->data, sectors)) {
			lock_release(&inode->lock);
			return 0;
		}
		inode->data.length = offset + size;
		cache_write(inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
	}

	while (size > 0)
	{
		/* Sector to write, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		check_sector(fs_device, sector_idx);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;


		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free(bounce);
	lock_release(&inode->lock);
	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write(struct inode* inode)
{
	inode->deny_write_cnt++;
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write(struct inode* inode)
{
	ASSERT(inode->deny_write_cnt > 0);
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length(const struct inode* inode)
{
	return inode->data.length;
}

bool inode_is_dir(const struct inode* inode) {
	return inode->data.is_dir;
}

void inode_set_dir(struct inode* inode, bool is_dir) {
	inode->data.is_dir = is_dir;
	cache_write(inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
}

bool alloc_inode_block(block_sector_t* sector) {
	static uint8_t zeros[BLOCK_SECTOR_SIZE];
	if (*sector == 0) {
		if (!free_map_allocate(1, sector)) {
			return false;
		}
		cache_write(*sector, zeros, 0, BLOCK_SECTOR_SIZE);
	}
	return true;
}

bool inode_update(InodeDisk* inode, int n) {
	int direct_cnt = (n <= DIRECT_BLOCK_NUM) ? n : DIRECT_BLOCK_NUM;
	n -= direct_cnt;

	// Allocate direct blocks first
	for(int i = 0; i < direct_cnt; i++) {
		if (!alloc_inode_block(&inode->direct[i])) {
			return false;
		}
	}
	if (n == 0) {
		return true;
	}
	// If there are still blocks to allocate, allocate indirect block
	int indirect_cnt;
	if (n % INDIRECT_BLOCK_NUM != 0) {
		indirect_cnt = n / INDIRECT_BLOCK_NUM + 1;
	}
	else {
		indirect_cnt = n / INDIRECT_BLOCK_NUM;
	}

	// Double indirect block
	InodeIndirect* double_indirect = malloc(BLOCK_SECTOR_SIZE);
	if (inode->double_indirect == 0) {
		// Means double indirect block not allocated
		if(!alloc_inode_block(&inode->double_indirect)) {
			free(double_indirect);
			return false;
		}
		// Initialize double indirect block
		memset(double_indirect, 0, BLOCK_SECTOR_SIZE);
	}
	else {
		cache_read(inode->double_indirect, double_indirect, 0, BLOCK_SECTOR_SIZE);
	}

	// Allocate indirect blocks
	for(int i = 0; i < indirect_cnt; i++) {
		int cnt = (n <= INDIRECT_BLOCK_NUM) ? n : INDIRECT_BLOCK_NUM;
		n -= cnt;

		// Allocate indirect block
		InodeIndirect* indirect = malloc(BLOCK_SECTOR_SIZE);
		block_sector_t* indirect_sector = &double_indirect->data[i];

		if (*indirect_sector == 0) {
			if(!alloc_inode_block(indirect_sector)) {
				free(double_indirect);
				free(indirect);
				return false;
			}
			memset(indirect, 0, BLOCK_SECTOR_SIZE);
		}
		else {
			cache_read(*indirect_sector, indirect, 0, BLOCK_SECTOR_SIZE);
		}
		for(int j = 0; j < cnt; j++) {
			if(!alloc_inode_block(&indirect->data[j])) {
				free(double_indirect);
				free(indirect);
				return false;
			}
		}
		cache_write(*indirect_sector, indirect, 0, BLOCK_SECTOR_SIZE);
		free(indirect);
	}
	cache_write(inode->double_indirect, double_indirect, 0, BLOCK_SECTOR_SIZE);
	free(double_indirect);
	return true;
}
