#pragma once
#include <devices/block.h>
#include <stdint.h>
#include <stdbool.h>
#include <threads/synch.h>

#define CACHE_SIZE 64
#define CACHE_UNUSED 1145141919

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


extern struct lock cache_lock;
extern struct semaphore write_behind_success;

extern struct list read_ahead_list;
extern struct semaphore read_ahead_success;

void write_behind(void);
void read_ahead2(void);
void read_ahead(block_sector_t);
void cache_init(void);
void cache_read(block_sector_t, void*, int,int);
void cache_write(block_sector_t, const void*,int,int);
struct cache* cache_find(block_sector_t);
struct cache* cache_new(block_sector_t);
struct cache* cache_evict(void);
void write_back_all_cache(void);