#ifndef CACHE
#define CACHE

#include <devices/block.h>
#include <stdint.h>
#include <stdbool.h>



typedef struct buffer_cache {
    block_sector_t sector_id;
    bool dirty;
    bool second_change;
    uint8_t data[64];

} BufferCache;

void cache_init(void);
void cache_read(block_sector_t, void*, int, int);
void cache_write(block_sector_t, const void*, int, int);
BufferCache* cache_find(block_sector_t);
BufferCache* cache_create(block_sector_t);
BufferCache* cache_evict(void);

#endif // CACHE