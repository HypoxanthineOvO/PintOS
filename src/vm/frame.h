#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/thread.h"
#include "page.h"

extern struct lock frame_lock;

typedef struct frame_table_entry{
    void* frame; // Address of Frame
    Page* corres_page; // corresponding user page
    struct thread* owner;

    struct hash_elem elem; // For frame table
} Frame;

void* frame_table_init(void);
Frame* frame_alloc(Page*);
void frame_free(void*);
#endif // VM_FRAME_H