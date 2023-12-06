# CS140 Project 3: Virtual Memory
## Group
- Yunxiang He <heyx1@shanghaitech.edu.cn>
- Yicheng Fan <fanych1@shanghaitech.edu.cn>

# Page table management
## Data Structures

### A1
Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.
```cpp
/* page.h */
typedef struct sup_page_entry{
    // Core elements
    void* user_virtual_addr;
    struct frame_table_entry* frame;
    bool writable;
    // For LRU
    uint64_t access_time;

    // File
    struct file* file;
    int32_t file_offset;
    uint32_t file_size;

    // Type Flag
    bool in_stack;
    
    // Table elem
    struct hash_elem elem;

} Page;

/* frame.h */
typedef struct frame_table_entry{
    void* kpage; // Address of Frame
    Page* corres_page; // corresponding user page
    struct thread* owner;

    struct hash_elem elem; // For frame table

    int flag;
} Frame;

/* frame.c */
static Hash frame_table;
struct lock frame_lock;

/* thread.h */
struct thread{
    ...
#ifdef VM
	struct hash page_table;
	void* esp;
#endif
    ...
}
```

## Algorithms

### A2
In a few paragraphs, describe your code for accessing the data stored in the SPT about a given page.
- Our SPT is a hash table.
- First, we use `page_find` to find the page in the SPT.
- If the page is in swap, we use `swap_in` to load the page from swap.
- If the page has a frame(`page->frame != NULL`), we can directly access the data in the frame.
- If the page is mmaped, we need to load the page from file.

### A3
How does your code coordinate accessed and dirty bits between kernel and user virtual addresses that alias a single frame, or alternatively how do you avoid the issue?
- We don't allow this aliasing to happen.
- We avoid accesing the memory using kernel virtual address. It's only used for page initialization.

## Synchornization

### A4
When two user processes both need a new frame at the same time, how are races avoided?
- We use lock to avoid races.
- In `frame.c`, we use a lock `frame_lock` to implement the synchronization.
- Before any operation on the frame table, we need to acquire the lock, and release it after the operation.

## Rationale

#### A5
Why did you choose the data structure(s) that you did for representing virtual-to-physical mappings?
- Noticed that Hash Table provides $O(1)$ time complexity for search, insert and delete operations, we choose it as our data structure.
- It's more efficient than other data structures like linked list, binary search tree, etc.

# Paging to and from disk
## Data Structures

### B1
Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.
```cpp
typedef struct sup_page_entry{
    ...
    // For Swap
    size_t swap_idx;
    ...
} Page;
```



## Algorithms

### B2
When a frame is required but none is free, some frame must be evicted.  Describe your code for choosing a frame to evict.
- We choose Second Chance algorithm to evict a frame.
- Theoritically, all pages are considered in a round robin matter.
- One page will be evict when it is visisted at second time.
- When we access a page, the second change bit will be reset to 0.

### B3
When a process P obtains a frame that was previously used by a process Q, how do you adjust the page table (and any other data structures) to reflect the frame Q no longer has?
- When Q no longer has the frame, we need to remove the frame from Q's SPT.
- That is implemented in `frame_free` function. After the frame is freed, `page->frame` will be set to NULL.
- Thus we can know that Q no longer has the frame.

### B4
Explain your heuristic for deciding whether a page fault for an invalid virtual address should cause the stack to be extended into the page that faulted.
- If following conditions are satisfied, we think the address is a valid stack access.
  - `addr >= PHYS_BASE`: The address is not in user virtual address space.
  - `addr <= esp - 32`: The address is not in the stack growth area.
  - The stack is not larger than 8MB.

## Synchronization
### B5
Explain the basics of your VM synchronization design.  In particular, explain how it prevents deadlock.  (Refer to the textbook for an explanation of the necessary conditions for deadlock.)
- In our implementation, we only use one lock `frame_lock` to implement synchronization.
- That means no "hold and wait" condition for deadlock happens.
- So, we don't need to worry about deadlock.

### B6
A page fault in process P can cause another process Q's frame to be evicted.  How do you ensure that Q cannot access or modify the page during the eviction process?  How do you avoid a race between P evicting Q's frame and Q faulting the page back in?
- We use `frame_lock` to ensure that Q cannot access or modify the page during the eviction process.
- When P evicts Q's frame, it will acquire the lock. The lock will be released after the eviction.
- When Q faults the page back in, it will also acquire the lock. The lock will be released after the page is loaded.
- So, there will be no race between P evicting Q's frame and Q faulting the page back in.

### B7
Suppose a page fault in process P causes a page to be read from the file system or swap.  How do you ensure that a second process Q cannot interfere by e.g. attempting to evict the frame while it is still being read in?
- We use `frame_lock` to ensure that Q cannot interfere by e.g. attempting to evict the frame while it is still being read in.

### B8
Explain how you handle access to paged-out pages that occur during system calls.  Do you use page faults to bring in pages (as in user programs), or do you have a mechanism for "locking" frames into physical memory, or do you use some other design?  How do you gracefully handle attempted accesses to invalid virtual addresses?
- We use page faults to bring in pages.
- When a page fault occurs, we will check whether the page is in SPT.
- If the page is in SPT, we will load the page from swap or file.
- If the page is not in SPT, we think it's an invalid virtual address and exit the process.

## Rationale
### B9
A single lock for the whole VM system would make synchronization easy, but limit parallelism.  On the other hand, using many locks complicates synchronization and raises the possibility for deadlock but allows for high parallelism.  Explain where your design falls along this continuum and why you chose to design it this way.
- PitOS is a single core system, so we don't need to worry about parallelism.
- Thus we choose to use single lock for the whole VM system to make synchronization easy.

# Memory mapped files
## Data Structures
### C1
Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.

## Algorithms
### C2
Describe how memory mapped files integrate into your virtual memory subsystem.  Explain how the page fault and eviction processes differ between swap pages and other pages.

### C3
Explain how you determine whether a new file mapping overlaps any existing segment.

## Rationale

### C4
Mappings created with "mmap" have similar semantics to those of data demand-paged from executables, except that "mmap" mappings are written back to their original files, not to swap.  This implies that much of their implementation can be shared.  Explain why your implementation either does or does not share much of the code for the two situations.
