#include "frame.h"

#include <hash.h>

#include "threads/palloc.h"
#include "userprog/pagedir.h"

static Hash frame_table;
struct lock frame_lock;

// Utils
static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED) {
    const Frame *f = hash_entry (e, Frame, elem);
    return hash_bytes (&f->kpage, sizeof (void *));
}

static frame_less(const struct hash_elem* a, const struct hash_elem* b){
    const Frame* x = hash_entry(a, Frame, elem);
    const Frame* y = hash_entry(b, Frame, elem);
    return x->kpage < y->kpage;
};


void* frame_table_init(){
    hash_init(&frame_table, frame_hash, frame_less, NULL);
    lock_init(&frame_lock);
}

Frame* frame_alloc(Page* user_page){
    void* kernel_page = palloc_get_page(PAL_USER);
    if (kernel_page == NULL) {
        // Evict
        frame_evict();

        kernel_page = palloc_get_page(PAL_USER);
        if(kernel_page == NULL)return NULL;
    }
    Frame* frame = malloc(sizeof(Frame));
    if (frame == NULL){
        puts("Frame allocation failed");
        return NULL;
    }
    frame->kpage = kernel_page;
    frame->corres_page = user_page;
    hash_insert(&frame_table, &frame->elem);
    frame->owner = thread_current();
    frame->flag = 1;
    return frame;
}

void frame_free(Frame* frame){
    // if (frame_addr == NULL) return;
    // // For Each Hash Table
    // struct hash_iterator it;
    // hash_first(&it, &frame_table);
    // while(hash_next(&it)){
    //     Frame* frame = hash_entry(hash_cur(&it), Frame, elem);
    //     if (frame->kpage == frame_addr){
    //         hash_delete(&frame_table, hash_cur(&it));
    //         palloc_free_page(frame_addr);
    //         free(frame);
    //         break;
    //     }
    // }
    hash_delete (&frame_table, &frame->elem);
    pagedir_clear_page (frame->owner->pagedir, frame->corres_page->user_virtual_addr);
    palloc_free_page (frame->kpage);
    free (frame);
}

void frame_evict(){
    struct hash_iterator it;
    hash_first(&it, &frame_table);
    Frame* frame = NULL;
    while(hash_next(&it)){
        frame = hash_entry(hash_cur(&it), Frame, elem);
        if (frame->owner->tid == 0) continue;
        if (frame->flag == 0) break;
        frame->flag = 0;
    }
    if (frame == NULL) return;
    swap_out(frame->corres_page);
    frame_free(frame);
    return;
}