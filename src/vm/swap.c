#include "swap.h"
#include "devices/block.h"
#include "filesys/file.h"
#include "frame.h"
#include "page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <bitmap.h>

#define PAGE_BLOCKS (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block* swap_device;
static struct bitmap* swap_table;
static struct lock swap_lock;

void swap_init(void) {
	// Initialize the swap device
	swap_device = block_get_role(BLOCK_SWAP);
	ASSERT(swap_device);
	// Initialize the swap bitmap
	size_t swap_size = block_size(swap_device) / PAGE_BLOCKS;
	swap_table = bitmap_create(swap_size);
	ASSERT(swap_table);
	// Initialize the swap lock
	lock_init(&swap_lock);
}

// Read the page from the swap slot
void swap_in(Page* page) {
	lock_acquire(&swap_lock);
	bool valid = bitmap_test(swap_table, page->swap_index);
	if(!valid) {
		PANIC("Invalid swap index");
	}
	for (int i = 0; i < PAGE_BLOCKS; i++) {
		block_read(swap_device, page->swap_index * PAGE_BLOCKS + i, page->frame->kpage + i * BLOCK_SECTOR_SIZE);
	}
	bitmap_reset(swap_table, page->swap_index);
	page->swap_index = BITMAP_ERROR; // Mark the page as not in the swap slot
	lock_release(&swap_lock);
}

// Write the page to the swap slot
void swap_out(Page* page) {
	if (page->file) {
		// The page is in the file system, write it back to the file system
		ASSERT(page->swap_index == BITMAP_ERROR);
		ASSERT(page->frame && page->frame->kpage);
		// Do Write Back
		uint32_t* pagedir = page->frame->owner->pagedir;
		void* virtual_addr = page->user_virtual_addr;
		if (pagedir_is_dirty(pagedir, virtual_addr)) {
			// The page is dirty, write it back to the file system
			pagedir_set_dirty(pagedir, virtual_addr, false);
			file_write_at(page->file, page->frame->kpage, page->file_size, page->file_offset);
		}
		// Free the frame
		page->frame = NULL;
		return;
	}
	else {
		lock_acquire(&swap_lock);
		size_t sector = bitmap_scan_and_flip(swap_table, 0, 1, false);
		if (sector == BITMAP_ERROR) {
			PANIC("Swap is full");
		}
		page->swap_index = sector;
		for (int i = 0; i < PAGE_BLOCKS; i++) {
			block_write(swap_device, page->swap_index * PAGE_BLOCKS + i, page->frame->kpage + i * BLOCK_SECTOR_SIZE);
		}
		page->frame = NULL;
		lock_release(&swap_lock);
		return;
	}
	NOT_REACHED();
}

// Free the swap slot
void swap_free(size_t id) {
	lock_acquire(&swap_lock);
	bitmap_reset(swap_table, id);
	lock_release(&swap_lock);
}
