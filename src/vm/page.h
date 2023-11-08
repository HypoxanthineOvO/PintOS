#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/file.h"

#define STACK_LIMIT 8388608

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

void page_table_init(Hash*);
void page_table_destroy(Hash*);
Page* page_find(Hash* , void* );
void page_free(Hash* , Page*);
bool page_fault_handler(Hash* , void* , void* , bool);
Page* page_create_on_stack(Hash*, void*);
Page* page_create_out_stack(Hash*, void*, bool, struct file*, int32_t, uint32_t);
#endif // VM_PAGE_H