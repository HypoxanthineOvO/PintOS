#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

struct bitmap;
#define DIRECT_BLOCK_NUM 124
#define INDIRECT_BLOCK_NUM 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
typedef struct inode_disk
{
	off_t length;                       /* File size in bytes. */
	unsigned is_dir; // 1 if this inode is a directory, 0 if not
    unsigned magic;                     /* Magic number. */
	block_sector_t direct[DIRECT_BLOCK_NUM]; // 124 direct blocks
    block_sector_t indirect; // 1 double-indirect block
} InodeDisk;


/* In-memory inode. */
typedef struct inode
{
	struct list_elem elem;              /* Element in inode list. */
	block_sector_t sector;              /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
    struct lock lock; // Lock for inode
} Inode;

typedef struct indirect_inode
{
    block_sector_t data[INDIRECT_BLOCK_NUM];
} InodeIndirect;


void inode_init (void);
bool inode_create (block_sector_t, off_t);
Inode *inode_open (block_sector_t);
Inode *inode_reopen (Inode *);
block_sector_t inode_get_inumber (const Inode *);
void inode_close (Inode *);
void inode_remove (Inode *);
off_t inode_read_at (Inode *, void *, off_t size, off_t offset);
off_t inode_write_at (Inode *, const void *, off_t size, off_t offset);
void inode_deny_write (Inode *);
void inode_allow_write (Inode *);
off_t inode_length (const Inode *);

/* For Project 4 */
bool inode_is_dir(const Inode*);
void inode_set_dir(Inode*, bool);
bool alloc_inode_block(block_sector_t*);
bool extend_inode(InodeDisk* , off_t);
#endif /* filesys/inode.h */
