#include "page.h"

#include <bitmap.h>
#include <stdlib.h>
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "frame.h"
#include "swap.h"

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

void page_source_free(Page* page) {
    if (page->file) {
        page_write_back(page);
    }
    else {
        if(page->swap_idx != BITMAP_ERROR) swap_free(page->swap_idx);
        frame_free(page->frame);
    }
}
// Util Function for page_fault_handler
bool valid_page_access(Page* page, void* addr, void* esp, bool is_write) {
    if(addr == 0 || !is_user_vaddr(addr)) return false;
    if(page) {
        if(!page->writable && is_write) return false;
        if(page->in_stack && !addr_in_stack(addr, esp)) return false;
    }
    else {
        if(!addr_in_stack(addr, esp)) return false;
    }
    return true;
}

void load_from_file(Page* page) {
    // Load From File
    page->frame = frame_alloc(page);
    ASSERT (page->frame);
    ASSERT(file_read_at(
        page->file, page->frame->kpage, page->file_size, page->file_offset
    ) == page->file_size);
    memset(
        page->frame->kpage + page->file_size, 0, PGSIZE - page->file_size
    );
}

void load_from_swap(Page* page) {
    page->frame = frame_alloc(page);
    if (page->swap_idx != BITMAP_ERROR){
        swap_in(page);
    }
    else{
        memset(page->frame->kpage, 0, PGSIZE);
    }
}
// Implementation

void page_table_init(Hash* table){
    hash_init(table, page_hash, page_less, NULL);
}

void page_destroy(struct hash_elem* e, void* aux UNUSED) {
    Page* page = hash_entry(e, Page, elem);
    page_source_free(page);
    free(page);
}

void page_table_destroy(Hash* table){
    // The lock is must for page parrallel.
    lock_acquire(&frame_lock);
    hash_destroy(table, page_destroy);
    lock_release(&frame_lock);
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

void page_write_back(Page* page) {
    if (page->frame && pagedir_is_dirty(page->frame->owner->pagedir, page->user_virtual_addr)){
        file_write_at(page->file, page->user_virtual_addr, page->file_size, page->file_offset);
        pagedir_set_dirty(page->frame->owner->pagedir, page->user_virtual_addr, false);
        frame_free(page->frame);
        page->frame = NULL;
    }
}

void page_free(Hash* table, Page* page){
    page_source_free(page);
    // Remove from hash table
    hash_delete(table, &page->elem);
    free(page);
}


bool page_fault_handler(struct hash* page_table, void* addr, void* esp, bool read_write_state){
    lock_acquire(&frame_lock);
    // Try Get Page
    Page* page = page_find(page_table, addr);
    if(!valid_page_access(page, addr, esp, read_write_state)){
        lock_release(&frame_lock);
        return false;
    }
    if (page){
        if (page->file) load_from_file(page);
        else load_from_swap(page);
    }
    else {
        page = page_create_on_stack(page_table, addr);
    }
    // Install Page
    if (!install_page(page->user_virtual_addr, page->frame->kpage, page->writable)){
        page_free(page_table, page);
        lock_release(&frame_lock);
        return false;
    }
    lock_release(&frame_lock);
    return true;
}

Page* page_create_on_stack(Hash* table, void* addr){
    //puts("CREATE STACK!!!");
    Page* page = malloc(sizeof(Page));
    if (page == NULL) {
        return NULL;
    }
    page->user_virtual_addr = pg_round_down(addr);
    page->frame = frame_alloc(page);
    if(page->frame == NULL){
        free(page);
        return NULL;
    }
    memset(page->frame->kpage, 0, PGSIZE);
    page->file = NULL;
    page->writable = true;
    page->in_stack = true;
    page->swap_idx = BITMAP_ERROR;

    ASSERT(!hash_insert(table, &page->elem));
    return page;
}

Page* page_create_out_stack(
    Hash* page_table, void* user_addr, bool writable, 
    struct file* file, int32_t file_offset, uint32_t file_size
){
    Page* page = malloc(sizeof(Page));
    if (page == NULL) return NULL;
    
    page->user_virtual_addr = pg_round_down(user_addr);
    page->frame = NULL;

    page->file = file;
    page->file_offset = file_offset;
    page->file_size = file_size;
    
    page->writable = writable;
    page->in_stack = false;
    page->swap_idx = BITMAP_ERROR;
    if (hash_insert(page_table, &page->elem)){
        free(page);
        ASSERT(false);
        return NULL;
    }
    return page;
}