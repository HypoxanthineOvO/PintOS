#pragma once

#include <devices/block.h>
#include <stdint.h>
#include <stdbool.h>
#include "threads/synch.h"

#define CACHE_SIZE 64

typedef struct buffer_cache {
    block_sector_t sector_id;
    bool dirty;
    bool second_chance_flag;
    uint8_t data[64];
    struct lock lock;
} BufferCache;

void cache_init();
void cache_read(block_sector_t, void*, int, int);
void cache_write(block_sector_t, const void*, int, int);
BufferCache* cache_find(block_sector_t);
BufferCache* cache_create(block_sector_t);
BufferCache* cache_evict(void);

void write_back_all_cache();
