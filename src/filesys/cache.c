#include "devices/timer.h"
#include "threads/thread.h"
#include "cache.h"
#include "filesys.h"


static BufferCache cache[CACHE_SIZE];
struct lock cache_lock;
struct semaphore cache_sema_read, cache_sema_write;

typedef struct read_ahead_entry{
    block_sector_t sector_id;
    struct list_elem elem;
} ReadAheadEntry;
static struct list read_ahead_list;

void write_behind() {
    sema_init(&cache_sema_write, 0);
    while(true) {
        write_back_all_cache();
        timer_sleep(1000);
    }
    sema_up(&cache_sema_write);
}

void read_ahead(){
    while(true){
        sema_down(&cache_sema_read);
        ReadAheadEntry* entry = list_entry(list_pop_front(&read_ahead_list), ReadAheadEntry, elem);
        BufferCache* c = cache_create(entry->sector_id);
        block_read(fs_device, entry->sector_id, c->data);
        free(entry);
    }
}

void cache_init(){
    lock_init(&cache_lock);
    lock_acquire(&cache_lock);
    sema_init(&cache_sema_read, 0);
    list_init(&read_ahead_list);
    for(int i=0;i<CACHE_SIZE;i++){
        cache[i].sector_id = -1;
        cache[i].dirty = false;
        cache[i].second_chance_flag = true;
        lock_init(&cache[i].lock);
    }
    // Create Threads
    thread_create("write_behind", PRI_DEFAULT, (thread_func *)write_behind, NULL);
    thread_create("read_ahead", PRI_DEFAULT, (thread_func *)read_ahead, NULL);
    lock_release(&cache_lock);

    puts("Cache Init Done");
}

void cache_read(block_sector_t id, void* data, int offset, int size){
    //puts("READ!!");
    //lock_acquire(&cache_lock);
    BufferCache* c = cache_find(id);
    if (c){
        //lock_acquire(&c->lock);
        //lock_release(&cache_lock);
    }
    else {// C == NULL
        c = cache_create(id);
        //lock_acquire(&c->lock);
        //lock_release(&cache_lock);
        block_read(fs_device, id, c->data);
    }
    memcpy(data, c->data + offset, size);
    //lock_release(&c->lock);
}

void cache_write(block_sector_t id, const void* data, int offset, int size) {
    //puts("Write!!");
    //lock_acquire(&cache_lock);
    BufferCache* c = cache_find(id);
    
    if (c) {
        //lock_acquire(&c->lock);
        //lock_release(&cache_lock);
    }
    else {
        c = cache_create(id);
        //lock_acquire(&c->lock);
        //lock_release(&cache_lock);
        block_read(fs_device, id, c->data);
    }
    //puts("Write Mid!!");
    c->dirty = true;
    memcpy(c->data + offset, data, size);
    
    //lock_release(&c->lock);
    
}

BufferCache* cache_find(block_sector_t id){
    for(int i = 0; i < CACHE_SIZE; i++) {
        if(cache[i].sector_id == id) {
            cache[i].second_chance_flag = true; // Update second chance flag
            return &cache[i];
        }
    }
    return NULL;
}

BufferCache* cache_create(block_sector_t id) {
    BufferCache* c = NULL;
    for(int i = 0; i < CACHE_SIZE; i++) {
        if(cache[i].sector_id == -1) {
            c = &cache[i];
            break;
        }
    }
    if(c == NULL) {
        // Cache is full, evict a cache
        c = cache_evict();
    }
    c->sector_id = id;
    c->dirty = false;
    c->second_chance_flag = true;
    return c;
}

BufferCache* cache_evict() {
    BufferCache* c = NULL;
    int try_count = 10;
    while(try_count--) {
        for(int i = 0; i < CACHE_SIZE; i++) {
            bool syn = lock_try_acquire(&cache[i].lock);
            if(syn) {
                if(cache[i].second_chance_flag) {
                    cache[i].second_chance_flag = false;
                    lock_release(&cache[i].lock);
                    continue;
                }
                else {
                    c = &cache[i];
                    break;
                }
            }
        }
        if(c == NULL) continue;
        // Evict this cache
        // If dirty, write back to disk
        if(c->dirty) {
            block_write(fs_device, c->sector_id, c->data);
            c->dirty = false;
        }
        c->sector_id = -1;
        lock_release(&c->lock);
    }
    if(c == NULL){
        PANIC("Evict Failed");
    }
    return c;
}

void write_back_all_cache() {
    lock_acquire(&cache_lock);
    for(int i = 0; i < CACHE_SIZE; i++) {
        if(cache[i].sector_id != -1 && cache[i].dirty) {
            block_write(fs_device, cache[i].sector_id, cache[i].data);
            cache[i].dirty = false;
        }
    }
    lock_release(&cache_lock);
}