# CS140 Project 4: File Systems Design Document
## Group
- **Yunxiang He** <heyx1@shanghaitech.edu.cn>
- **Yicheng Fan** <fanych1@shanghaitech.edu.cn>

# Indexed And Extensible Files
## Data Structures
### A1: Copy here the declaration of each new or changed "struct" or "struct" member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.
```C++
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
typedef struct inode_disk
{
    ...
	block_sector_t direct[DIRECT_BLOCK_NUM]; // 124 direct blocks
    block_sector_t indirect; // 1 indirect block
} InodeDisk;


/* In-memory inode. */
typedef struct inode
{
	bool removed;                       /* True if deleted, false otherwise. */
    struct lock lock; // Lock for inode
} Inode;

typedef struct indirect_inode
{
    block_sector_t data[INDIRECT_BLOCK_NUM];
} InodeIndirect;

```

## Algorithms

### A2: What is the maximum size of a file supported by your inode structure?  Show your work.
- We use 124 direct blocks and 1 double indirect blocks.
- One double indirect block point to 128 indirect blocks.
- So, our File system can support `(128 * 128 + 124) * 512 Byte = 8316 KB` File Size.

## Rational
### A3: Explain how your code avoids a race if two processes attempt to extend a file at the same time.
- Our inode has a lock for IO synchronization.
- When a process is extending a file, the lock will be acquired after any operation. The lock will be released only when the extending is done.
- So, the two process will not extend the file at same time.

### A4: Suppose processes A and B both have file F open, both positioned at end-of-file.  If A reads and B writes F at the same time, A may read all, part, or none of what B writes.  However, A may not read data other than what B writes, e.g. if B writes nonzero data, A is not allowed to see all zeros.  Explain how your code avoids this race.
- If A read first, it will acquire lock and at the same time, B can not write until A end it's read. Thus the race will not happen.
- If B write first, it will acquire lock and A can read only after B write done. So the race will not happen.

### A5: Explain how your synchronization design provides "fairness". File access is "fair" if readers cannot indefinitely block writers or vice versa.  That is, many processes reading from a file cannot prevent forever another process from writing the file, and many processes writing to a file cannot prevent another process forever from reading the file.
- When a process finishes reading or writing a inode, it will release the lock of the inode immediately.
- So a process can't prevent another process from reading or writing the file forever.

## Rationale
### A6: Is your inode structure a multilevel index?  If so, why did you choose this particular combination of direct, indirect, and doubly indirect blocks?  If not, why did you choose an alternative inode structure, and what advantages and disadvantages does your structure have, compared to a multilevel index?
- Yes. We use 124 direct block and double indirect block.
- We only have 128 blocks, and 124 direct block is enough for most files. So we don't need to use indirect block.
- For those files that are larger than 124 * 512 KB, we use double indirect block to store the block number of indirect block, which can store 128 * 128 * 512 KB data.

# Subdirectories
## Data Structures
### B1: Copy here the declaration of each new or changed "struct" or "struct" member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.
```C++
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
typedef struct inode_disk
{
	unsigned is_dir; // 1 if this inode is a directory, 0 if not
} InodeDisk;

/* thread.c */
struct thread_node {
	int file_descriptor; // num of file descriptor
	bool is_dir;
	struct file* file; // file in the thread
	struct dir* dir;
	struct list_elem elem; // file list elem
};

struct thread {
    ...
	struct dir* cwd;
    ...
}
```

## Algorithms
### B2: Describe your code for traversing a user-specified path.  How do traversals of absolute and relative paths differ?
Firstly, we split basename and parent path.
- If the path is absolute(Start with "/"), we start from the root directory.
- If the path is relative, we start from the current working directory.
Then, we traverse the path.
- If it is an absolute path, we traverse the path from the root directory.
- If it is a relative path, we traverse the path from the current working directory.


## Synchronization
### B4: How do you prevent races on directory entries?  For example, only one of two simultaneous attempts to remove a single file should succeed, as should only one of two simultaneous attempts to create a file with the same name, and so on.
- By using a filesys lock, our file system module is single thread.
- That means, any file operation will be done one by one, no two file operation will be executed at the same time.

### B5: Does your implementation allow a directory to be removed if it is open by a process or if it is in use as a process's current working directory?  If so, what happens to that process's future file system operations?  If not, how do you prevent it?
- Before removing a directory, we will check if it is open by a process or if it is in use as a process's current working directory by using an `open_cnt` in inode structure.
- If `open_cnt == 1`, which means only the current process is using the directory, we can remove it.

## Rational
### B6: Explain why you chose to represent the current directory of a process the way you did.
- By adding `struct dir *cwd;` in the `struct thread` structure.
- The reason is that we need to know the current directory of a process. We doesn't allow a directory to be removed if it is open by a process or if it is in use as a process's current working directory. So it is nice to have a pointer to the current directory of a process, which prevents the directory from being removed.
- Besides, it's more convinient to use a pointer than to open the directory every time we need it.

# Buffer Cache
## Data Structures
### C1: Copy here the declaration of each new or changed "struct" or "struct" member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.
```C++
// File system cache
typedef struct cache
{
    block_sector_t sector_id; // The sector id
    bool dirty; // Whether this cache has changed
    bool second_chance; // Second chance algorithm
    uint8_t data[BLOCK_SECTOR_SIZE]; // The data block of the cache
    struct lock lock; // When read or write, lock
} Cache;

struct read_ahead_entry {
    struct list_elem elem; // The list element of readahead
    block_sector_t sector_id; // readahead sector id
};

extern struct lock cache_lock; // lock for cache
extern struct semaphore write_behind_success; // semaphore for write behind

extern struct list read_ahead_list; // The list for read ahead
extern struct semaphore read_ahead_success; // Semaphore for read ahead

static Cache cache[CACHE_SIZE]; // The array for cache block
```
## Algorithms
### C2: Describe how your cache replacement algorithm chooses a cache block to evict.

We consider the second chance algorithm to evict the cache block.When the cache is accessed, the flag for it will be changed and the cache will be evicted when visited twice.

### C3: Describe your implementation of write-behind.

For some ticks, we write dirty cache to the disk with a write behind thread, which runs during the filesys is on. To synchronize, we use semaphore `write_behind_success` to implement it.

### C4: Describe your implementation of read-ahead.

Similar to write behind, we also use a read ahead thread to read the data to cache. And also similar to write behind, we use semaphore `read_ahead_success`.Besides that, we pass the sector id with a list `read_ahead_list`, and then read the data to cache.

## Synchronization
### C5: When one process is actively reading or writing data in a buffer cache block, how are other processes prevented from evicting that block?

We have a lock for each cache. When reading or writing, the lock will lock the cache block. It ensures that the cache won't be evicted when being locked.

### C6: During the eviction of a block from the cache, how are other processes prevented from attempting to access the block?

Here we have a global lock `cache_lock`, the `cache_lock` will be locked when evicting. When doing cache operation, the thread will `lock_acquire(&cache_lock)`. After having done, `lock_release(&c->lock)`.Then other processes won't reach the block.

## Rationale
### C7: Describe a file workload likely to benefit from buffer caching, and workloads likely to benefit from read-ahead and write-behind.

- Access the same block for many times: benefit from buffer caching;
- Access the data sequentially: benefit from read-ahead;
- The file keeps open and not frequently written: benefit from write-behind.