/*
 * page.c
 *
 *  Created on: Dec 27, 2015
 *      Author: ashish
 */
#include "includes/page.h"
#include "includes/logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

#include <assert.h>

int is_power_of_2(uint32_t block_size) {
	if(block_size == 1) return 1;
	if(block_size == 0 || block_size % 2) return 0;
	return is_power_of_2(block_size >> 1);
	//return (__builtin_pop_cnt(block_size) == 1);
}

void* sbrk(intptr_t increment);

int is_page_aligned(void* page_ptr) {
	return ((uintptr_t)page_ptr % PAGE_SIZE == 0);
}


#define PAGES_PER_ALLOC 16
page_t* global_page_store_bottom = NULL;
page_t* global_page_store_top = NULL;
pthread_mutex_t global_page_store_lock = PTHREAD_MUTEX_INITIALIZER;

page_t* create_page_aligned(uint32_t block_size) {
	LOG_PROLOG();

	assert(is_power_of_2(block_size));

	page_t* ptr_page;

#define PAGE_TECHNIQUE 1
#if (PAGE_TECHNIQUE == 0)	
	posix_memalign((void**)&ptr_page, sizeof(page_t), sizeof(page_t));
#elif (PAGE_TECHNIQUE == 1)
	pthread_mutex_lock(&global_page_store_lock);
       
        if(global_page_store_bottom == global_page_store_top) {
	  void* new_mem = sbrk(PAGE_SIZE * PAGES_PER_ALLOC);
	  global_page_store_bottom = new_mem;
	  unsigned pages = PAGES_PER_ALLOC;
	  if(!is_page_aligned(global_page_store_bottom)) {
	    global_page_store_bottom = (void*)((((uintptr_t)global_page_store_bottom) + PAGE_SIZE) & PAGE_MASK);
	    pages--;
	  }
	  assert(is_page_aligned(global_page_store_bottom));
	  global_page_store_top = global_page_store_bottom + pages;
	}
	
	ptr_page = global_page_store_bottom;
	global_page_store_bottom++;
	assert(global_page_store_bottom <= global_page_store_top);
	


	pthread_mutex_unlock(&global_page_store_lock);
#endif
	ptr_page->header.block_size = block_size;
	ptr_page->header.max_blocks = ((sizeof(page_t) - sizeof(page_header_t)) / block_size);
	ptr_page->header.min_free_blocks = ptr_page->header.max_blocks;
	ptr_page->header.next_free_block_idx = 0;
	memset(ptr_page->header.block_flags, BLOCK_OCCUPIED, sizeof(ptr_page->header.block_flags));
	memset(ptr_page->header.block_flags, BLOCK_EMPTY, (ptr_page->header.max_blocks));
        
	INIT_LIST_HEAD(&(ptr_page->header.node));
	
	assert(ptr_page->header.max_blocks <= MAX_BLOCKS_IN_PAGE);
	assert(((uintptr_t)ptr_page) % PAGE_SIZE == 0);
	
	LOG_EPILOG();
	return ptr_page;
}

page_t* create_page(uint32_t block_size) {
	LOG_PROLOG();

	page_t *ptr_page = (page_t*) malloc(sizeof(page_t));
	ptr_page->header.block_size = block_size;
	ptr_page->header.max_blocks = ((sizeof(page_t) - sizeof(page_header_t)) / BLOCK_SIZE(block_size));
	ptr_page->header.min_free_blocks = ptr_page->header.max_blocks;
	ptr_page->header.next_free_block_idx = 0;
	memset(ptr_page->header.block_flags, BLOCK_OCCUPIED, sizeof(ptr_page->header.block_flags));
	memset(ptr_page->header.block_flags, BLOCK_EMPTY, (ptr_page->header.max_blocks));

	INIT_LIST_HEAD(&(ptr_page->header.node));

	LOG_EPILOG();
	return ptr_page;
}

int find_first_empty_block(page_t* ptr) {
	LOG_PROLOG();

	int block_idx = 0;
	int result = -1;
	page_header_t *header = &(ptr->header);
	if (header->next_free_block_idx != -1) {
		result = header->next_free_block_idx;
		ptr->header.next_free_block_idx = -1;
	} else {
		for (block_idx = 0; block_idx < header->max_blocks; ++block_idx) {
			if (header->block_flags[block_idx] == BLOCK_EMPTY) {
				result = block_idx;
				if ((block_idx + 1) < header->max_blocks) {
					if (header->block_flags[block_idx+1] == BLOCK_EMPTY) {
						ptr->header.next_free_block_idx = block_idx+1;
					}
				}

				break;
			}
		}
	}

	LOG_EPILOG();
	return result;
}

int count_empty_blocks(page_t* ptr) {
	LOG_PROLOG();

	int count = 0;

	int block_idx = 0;
	page_header_t *header = &(ptr->header);
	for (block_idx = 0; block_idx < header->max_blocks; ++block_idx) {
		if (header->block_flags[block_idx] == BLOCK_EMPTY) {
			count++;
			if (header->next_free_block_idx != -1) {
				ptr->header.next_free_block_idx = block_idx;
			}
		}
	}

	ptr->header.min_free_blocks = count;

	LOG_EPILOG();
	return count;
}


uint32_t get_min_free_blocks(page_t* ptr) {
	LOG_PROLOG();
	LOG_EPILOG();
	return ptr->header.min_free_blocks;
}

bool has_empty_block(page_t* ptr) {
	LOG_PROLOG();

	bool result = (find_first_empty_block(ptr) != -1);

	LOG_EPILOG();
	return result;
}

void* malloc_block(page_t* ptr) {
	assert(ptr);
	LOG_PROLOG();

	void* block = NULL;
	int block_idx = find_first_empty_block(ptr);
	if (block_idx != -1) {
		assert(ptr->header.block_flags[block_idx] == BLOCK_EMPTY);
		ptr->header.block_flags[block_idx] = BLOCK_OCCUPIED;
		//uint32_t block_size = ptr->header.block_size + sizeof(mem_block_header_t);
		uint32_t block_size = ptr->header.block_size;
		uint32_t byte_offset = sizeof(page_header_t) + (block_idx * block_size);
		block = ((char*) ptr) + byte_offset;

		ptr->header.min_free_blocks -= 1;

		uintptr_t pptr = (uintptr_t) ptr;
		uintptr_t bptr = (uintptr_t) block;
		assert(bptr >= pptr + sizeof(page_header_t));
		assert(bptr < pptr + PAGE_SIZE);
		assert(bptr);
		//assert(block_size >= 8 && bptr & WORD_BYTES == 0); //Check for word alignment
	}

	LOG_EPILOG();
	return block;
}

void* get_page_ptr(void *block_ptr) {
	LOG_PROLOG();
	
	void* page_ptr = (void*) ((uintptr_t) block_ptr & PAGE_MASK);
	assert(is_page_aligned(page_ptr));
        assert(((uintptr_t)block_ptr) > ((uintptr_t)page_ptr));
        assert(((uintptr_t)block_ptr) - ((uintptr_t)page_ptr) < PAGE_SIZE);

	LOG_EPILOG();
	return page_ptr;
}

uint32_t get_block_index(void* block_ptr, void* page_ptr) {
	LOG_PROLOG();
	
	uintptr_t offset = (uintptr_t) block_ptr - (uintptr_t) page_ptr;
	uint32_t blk_index = (offset - sizeof(page_header_t)) / ((page_header_t*) page_ptr)->block_size;
	assert(blk_index < ((page_header_t*) page_ptr)->max_blocks);
	LOG_EPILOG();
	return blk_index;
}
/*
void free_block(void *block_ptr) {
	LOG_PROLOG();

	mem_block_header_t *block_header = (mem_block_header_t*)((char*)block_ptr - sizeof(mem_block_header_t));
	page_t* page_ptr = (page_t*)((char*)block_header - block_header->byte_offset);
	page_ptr->header.block_flags[block_header->idx] = BLOCK_EMPTY;

	LOG_EPILOG();
}*/

//Contract:
// block_ptr shall be a pointer to memory in a page owned by some thread.
void free_block(void *block_ptr) {

	assert(block_ptr);

	LOG_PROLOG();
	
	page_t* page_ptr = (page_t*)get_page_ptr(block_ptr);

	uint32_t block_index = get_block_index(block_ptr, page_ptr);
	assert(page_ptr->header.block_flags[block_index] == BLOCK_OCCUPIED);

	page_ptr->header.block_flags[block_index] = BLOCK_EMPTY;

	LOG_EPILOG();
}
