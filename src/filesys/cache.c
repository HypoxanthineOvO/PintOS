#include "cache.h"
#include "devices/block.h"
#include "list.h"
#include "threads/synch.h"
#include "devices/timer.h"
#include "filesys.h"
#include "threads/thread.h"
#include "string.h"

static Cache cache[CACHE_SIZE];
struct lock cache_lock;
struct semaphore write_behind_success;
struct list read_ahead_list;
struct semaphore read_ahead_success;
struct read_ahead_entry;

void write_behind(void)
{
    sema_init(&write_behind_success, 0);
    while(!filesystem_shutdown) {
        write_back_all_cache();
        timer_sleep(200);
    }
    sema_up(&write_behind_success);
}

void read_ahead2(void)
{
    while(!filesystem_shutdown) {
        sema_down(&read_ahead_success);
        struct read_ahead_entry *e = list_entry(list_pop_front(&read_ahead_list), struct read_ahead_entry, elem);
        struct cache* c = cache_new(e->sector_id);
        block_read(fs_device, e->sector_id, c->data);
        free(e);
    }
}

void read_ahead(block_sector_t id)
{
    struct read_ahead_entry *e = malloc(sizeof(struct read_ahead_entry));
    e->sector_id = id;
    list_push_back(&read_ahead_list, &e->elem);
    sema_up(&read_ahead_success);
}

void cache_init(void)
{
    lock_init(&cache_lock);
    lock_acquire(&cache_lock);
    sema_init(&read_ahead_success, 0);
    list_init(&read_ahead_list);
    sema_init(&write_behind_success, 0);
    for(int i = 0; i < CACHE_SIZE; i++) {
        cache[i].sector_id = CACHE_UNUSED;
        cache[i].dirty = false;
        cache[i].second_chance = true;
        lock_init(&cache[i].lock);
    }
    thread_create ("read_ahead", PRI_DEFAULT, (thread_func *) read_ahead, NULL);
    thread_create ("write_behind", PRI_DEFAULT, (thread_func *) write_behind, NULL);
    lock_release(&cache_lock);
}

static void cache_flush(Cache* c)
{
    if (c->dirty) {
        block_write(fs_device, c->sector_id, c->data);
        c->dirty = false;
    }
    else
        return;
}
// Write back all dirty cache.
void write_back_all_cache(void)
{
    lock_acquire(&cache_lock);
    for(int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].sector_id == CACHE_UNUSED) {
            continue;
        }
        cache_flush(cache + i);
    }
    lock_release(&cache_lock);
}

// Find the cache contains sector <id>
// Return NULL if not found.
Cache* cache_find_by_id(block_sector_t id)
{
    for(int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].sector_id  ==  id) {
            cache[i].second_chance = true;
            return cache + i;
        }
    }
    return NULL;
}

static Cache* cache_help_func(block_sector_t id)
{
    Cache *c = cache_find_by_id(id);
    if (c == NULL) {
        c = cache_new(id);
        lock_acquire(&c->lock);
        lock_release(&cache_lock); // it is safe to release the global lock when we have locked the cache's lock.
        block_read(fs_device, id, c->data);
    } else {
        lock_acquire(&c->lock);
        lock_release(&cache_lock); // it is safe to release the global lock when we have locked the cache's lock.
    }
    return c;
}

// Load data from cache/disk to <data>
void cache_read(block_sector_t id, void* data, int offset, int size)
{
    lock_acquire(&cache_lock);
    Cache* c = cache_help_func(id);
    memcpy(data, c->data + offset, size);
    lock_release(&c->lock);
}

// Write data from <data> to cache
void cache_write(block_sector_t id, const void* data, int offset, int size)
{
    lock_acquire(&cache_lock);
    Cache* c = cache_help_func(id);
    c->dirty = true;
    memcpy(c->data + offset, data, size);
    lock_release(&c->lock);
}

Cache* cache_new(block_sector_t id)
{
    Cache *c = NULL;
    for(int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].sector_id == CACHE_UNUSED) {
            c = cache + i;
            break;
        }
    }
    if (c == NULL) {
        c = cache_evict();
    }
    if (c == NULL) {
        //PANIC("cache_new: cannot find a cache entry");
        return NULL;
    }
    c->second_chance = true;
    c->sector_id = id;
    c->dirty = false;
    return c;
}

// Evict a cache entry.
// Should be called when all cache entries are used.
Cache* cache_evict(void)
{
    Cache *c = NULL;
    for(int k = 1; k <= 10; k++) { // try hard to find a cache entry.
        for(int i = 0; i < CACHE_SIZE; i++) {
            if (lock_try_acquire(&cache[i].lock)) {
                if (cache[i].second_chance) {
                    cache[i].second_chance = false;
                    lock_release(&cache[i].lock);
                } 
                else {
                    c = cache + i;
                }
                break;
            }
        }
        if (c == NULL) {
            continue;
        }
        cache_flush(c);
        c->sector_id = CACHE_UNUSED;
        lock_release(&c->lock);
    }
    return c;
}
