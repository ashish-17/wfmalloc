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
#include <sys/mman.h>

#include <assert.h>

uint8_t* get_block_flags_pointer(page_t *page_ptr) {
	//return (uint8_t*)(((uintptr_t)page_ptr) + sizeof(page_header_t)); 
	return page_ptr->header.block_flags;
}

void set_block_flags(page_t *page_ptr, uint32_t idx, uint8_t flag) {
	assert(page_ptr->header.max_blocks > idx);
	uint8_t* block_flags = get_block_flags_pointer(page_ptr); 
	block_flags[idx] = flag;
}

uint8_t get_block_flags(page_t *page_ptr, uint32_t idx) {
	assert(page_ptr->header.max_blocks > idx);
	uint8_t* block_flags = get_block_flags_pointer(page_ptr); 
	return block_flags[idx];
}

// TODO: Word alignment
uint32_t num_blocks_in_page(uint32_t block_size) {
	return (PAGE_SIZE - sizeof(page_header_t)) / (block_size + 1);
}

uint32_t size_of_page_header(uint32_t block_size) {
	return (num_blocks_in_page(block_size) + sizeof(page_header_t));   
}

int is_power_of_2(uint32_t block_size) {
	if(block_size == 1) return 1;
	if(block_size == 0 || block_size % 2) return 0;
	return is_power_of_2(block_size >> 1);
}

int is_page_aligned(void* page_ptr) {
	return ((uintptr_t)page_ptr % PAGE_SIZE == 0);
}

page_t* create_npages_aligned(uint32_t block_size, uint32_t n) {
	LOG_PROLOG();

	assert(is_power_of_2(block_size));


	page_t* ptr_page = mmap(NULL, n * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	
	assert(ptr_page);
	assert(is_page_aligned(ptr_page));
	
	page_t *head_list = ptr_page;
	page_t *temp_list = NULL;
	
	for (int i = 0; i < n; i++) {
	    (ptr_page + i)->header.block_size = block_size;

	    (ptr_page + i)->header.max_blocks = num_blocks_in_page(block_size);
	    //(ptr_page + i)->header.min_free_blocks = ptr_page->header.max_blocks;
	    (ptr_page + i)->header.next_free_block_idx = 0;
	    memset(get_block_flags_pointer((ptr_page + i)), BLOCK_EMPTY, num_blocks_in_page(block_size));

	    INIT_LIST_HEAD(&((ptr_page + i)->header.node));
	    init_wf_queue_node(&((ptr_page + i)->header.wf_node));
	
	    if (temp_list) {
		    temp_list->header.wf_node.next = &((ptr_page + i)->header.wf_node);
	    }
	    temp_list = (ptr_page + i);
	    assert((ptr_page + i)->header.max_blocks <= MAX_BLOCKS_IN_PAGE);
	    assert(is_page_aligned(ptr_page + i));
	    //assert(((uintptr_t)(ptr_page + i)) % PAGE_SIZE == 0);
	}

	LOG_EPILOG();
	return head_list;
}

int find_first_empty_block(page_t*ptr) {
    LOG_PROLOG();

    int block_idx = -1;
    page_header_t *header = &(ptr->header);

    assert(header->next_free_block_idx < header->max_blocks);

    int i;
    for (i = header->next_free_block_idx; i < header->max_blocks; i++) {
        if (get_block_flags(ptr, i) == BLOCK_EMPTY) {
	    block_idx = i;
	    break;
	}
    }

    //calculate next_free_block_idx for next time

    // i == header->max_blocks --> no free block
    // i == header->max_blocks - 1 --> the last block was free
    if (i == header->max_blocks || i == header->max_blocks - 1) { // reached the end
	header->next_free_block_idx = 0;
    } else {
	header->next_free_block_idx = i + 1;
    }

    LOG_EPILOG();
    return block_idx;
}

int count_empty_blocks(page_t* ptr) {
	LOG_PROLOG();

	int count = 0;

	int block_idx = 0;
	page_header_t *header = &(ptr->header);
	for (block_idx = 0; block_idx < header->max_blocks; ++block_idx) {
		if (get_block_flags(ptr, block_idx) == BLOCK_EMPTY) {
			count++;
			if (header->next_free_block_idx != -1) {
				ptr->header.next_free_block_idx = block_idx;
			}
		}
	}
	//ptr->header.min_free_blocks = count;
	LOG_EPILOG();
	return count;
}

/*
uint32_t get_min_free_blocks(page_t* ptr) {
	LOG_PROLOG();
	LOG_EPILOG();
	return ptr->header.min_free_blocks;
}
*/
uint32_t get_max_blocks(page_t* ptr) {
	return ptr->header.max_blocks;
}

/*
bool has_empty_block(page_t* ptr) {
	LOG_PROLOG();

	bool result = (find_first_empty_block(ptr) != -1);

	LOG_EPILOG();
	return result;
}
*/
void* malloc_block(page_t* ptr) {
	assert(ptr);
	LOG_PROLOG();

	//uint32_t block_size = ptr->header.block_size + sizeof(mem_block_header_t);
	uint32_t block_size = ptr->header.block_size;

	void* block = NULL;
	int block_idx = find_first_empty_block(ptr);
	assert(ptr->header.max_blocks == num_blocks_in_page(block_size));
	if (block_idx != -1) {
		assert(block_idx < ptr->header.max_blocks);
		assert(get_block_flags(ptr,block_idx) == BLOCK_EMPTY);
		set_block_flags(ptr, block_idx, BLOCK_OCCUPIED);
		uint32_t byte_offset = size_of_page_header(block_size) + (block_idx * block_size);

		block = ((char*) ptr) + byte_offset;

		//printf("page = %p, block = %p, sizeof(page_header) = %u, size_of_page_header(block_size) = %u, max_blocks = %u, block_index = %u, block_size = %u, byte_offset = %u\n", ptr, block, sizeof(page_header_t),  size_of_page_header(block_size), ptr->header.max_blocks, block_idx, block_size, byte_offset);
		assert(byte_offset < PAGE_SIZE);

		//ptr->header.min_free_blocks -= 1;

		uintptr_t pptr = (uintptr_t) ptr;
		uintptr_t bptr = (uintptr_t) block;
		assert(bptr >= pptr + size_of_page_header(block_size));
		assert(bptr < pptr + PAGE_SIZE);
		assert(bptr + block_size <= pptr + PAGE_SIZE);
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
	uint32_t blk_index = (offset - size_of_page_header(((page_t*)page_ptr)->header.block_size)) / ((page_header_t*) page_ptr)->block_size;
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
	assert(get_block_flags(page_ptr, block_index) == BLOCK_OCCUPIED);

	//page_ptr->header.block_flags[block_index] = BLOCK_EMPTY;
	set_block_flags(page_ptr, block_index, BLOCK_EMPTY);

	LOG_EPILOG();
}
