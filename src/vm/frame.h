#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/thread.h"
#include "page.h"

extern struct lock frame_lock;

typedef struct frame_table_entry{
    void* kpage; // Address of Frame
    Page* corres_page; // corresponding user page
    struct thread* owner; // The thread owns the frame

    struct hash_elem elem; // For frame table

    int flag; // Applying second chance algorithm
} Frame;

void* frame_table_init(void);
Frame* frame_alloc(Page*);
void frame_free(Frame*);
void frame_evict(void);
#endif // VM_FRAME_H