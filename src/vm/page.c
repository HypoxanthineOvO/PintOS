#include "page.h"

#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "frame.h"

static struct lock frame_lock;
// From userprog
extern bool install_page (void *, void *, bool);
// Utils

bool addr_in_stack(void* addr, void* esp){
    return (addr >= (PHYS_BASE - STACK_LIMIT)) && (addr >= esp - 32);
}

static unsigned page_hash (const struct hash_elem *e, void *aux UNUSED) {
    const Page *p = hash_entry (e, Page, elem);
    return hash_bytes (&p->user_virtual_addr, sizeof (void *));
}

static bool page_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED) {
    const Page *x = hash_entry (a, Page, elem);
    const Page *y = hash_entry (b, Page, elem);
    return x->user_virtual_addr < y->user_virtual_addr;
}

// Implementation

void page_table_init(Hash* table){
    hash_init(table, page_hash, page_less, NULL);
    // puts("=== Page Table Initialized ===");
    // printf("Hash Table Size: %d\n", table->bucket_cnt);
    // printf("Elem Count: %d\n", table->elem_cnt);
}

void page_table_destroy(Hash* table){
    //lock_acquire(&frame_lock);
    while(table->elem_cnt){
        struct hash_iterator it;
        hash_first(&it, table);
        hash_next(&it);
        Page* page = hash_entry(hash_cur(&it), Page, elem);
        page_free(table, page);
        
    }
    //lock_release(&frame_lock);
    hash_destroy(table, NULL);
}

Page* page_find(Hash* table, void* addr){
    if (table == NULL || addr == NULL){
        return NULL;
    }
    void* user_page = pg_round_down(addr);
    Page page;
    page.user_virtual_addr = user_page;
    struct hash_elem* elem = hash_find(table, &page.elem);
    if (elem == NULL) return NULL;
    return hash_entry(elem, Page, elem);
}

void page_free(Hash* table, Page* page){
    // Free page's source
    // TODO

    // Remove from hash table
    hash_delete(table, &page->elem);
    free(page);
}

bool page_fault_handler(struct hash* page_table, void* addr, void* esp, bool read_write_state){
    // Check Valid Address
    if (addr == 0 || !is_user_vaddr(addr)) {
        return false;
    }
    // Try Get Page
    Page* page = page_find(page_table, addr);
    if (page){
        /* Permission Check */
        if(!page->file_writable && read_write_state){
            return false;
        }
        /* Memory Location Check */
        if (!addr_in_stack(addr, esp) && page->in_stack){
            return false;
        }

        // Check From File or From Swap
        if (page->file){
            // Load From File
            page->frame = frame_alloc(page);
            if (page->frame == NULL) return false;
            size_t bytes_read = file_read_at(
                page->file, page->frame->frame, page->file_size, page->file_offset
            );
            
            if (bytes_read != page->file_size) return false;
            memset(
                page->frame->frame + page->file_size, 0, PGSIZE - page->file_size
            );
        }
        else{
            // Load From Swap
            page->frame = frame_alloc(page);
            memset(page->frame->frame, 0, PGSIZE);
        }
    }
    else {
        // Grow Stack
        if (!addr_in_stack(addr, esp)) {
            return false;
        }
        Page* page = page_create_on_stack(addr);
        page->writable = true;
    }
    // Install Page
    if (!install_page(page->user_virtual_addr, page->frame->frame, page->file_writable)){
        frame_free(page->frame->frame);
        return false;
    }
    return true;
}

Page* page_create_on_stack(void* addr){
    struct thread* cur = thread_current();
    Page* page = malloc(sizeof(Page));
    page->user_virtual_addr = pg_round_down(addr);
    page->file = NULL;

    page->access_time = timer_ticks();
    page->writable = true;
    page->in_stack = true;
    if (page == NULL) return false;

    /* Allocate New Frame */
    Frame* frame = frame_alloc(page);
    page->frame = frame;
    bool success = (
        install_page(page->user_virtual_addr, frame->frame, true)
        &&
        // Page Exists
        !hash_insert(&cur->page_table, &page->elem)
    );
    if (!success){
        frame_free(frame);
        return false;
    }
    page->in_stack = true;

    return page;
}

Page* page_create_out_stack(
    Hash* page_table, void* user_addr, bool writable, 
    struct file* file, int32_t file_offset, uint32_t file_size
){
    Page* page = malloc(sizeof(Page));
    if (page == NULL) return NULL;
    //puts("=== Page Created ===");
    
    page->user_virtual_addr = pg_round_down(user_addr);
    page->file = file;
    page->file_offset = file_offset;
    page->file_writable = writable;
    page->file_size = file_size;
    page->in_stack = false;
    if (hash_insert(page_table, &page->elem)){
        free(page);
        ASSERT(false);
        return NULL;
    }
    return page;
}