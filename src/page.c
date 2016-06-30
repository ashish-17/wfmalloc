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

page_t* create_npages(uint32_t block_size, uint32_t n) {
	LOG_PROLOG();

	page_t *ptr_page_head = (page_t*) malloc(sizeof(page_t)*n);
	uint32_t pg_idx = 0;
	for (pg_idx = 0; pg_idx < n; ++pg_idx) {
		(ptr_page_head + pg_idx)->header.block_size = block_size;
		(ptr_page_head + pg_idx)->header.max_blocks = ((sizeof(page_t) - sizeof(page_header_t)) / BLOCK_SIZE(block_size));
		(ptr_page_head + pg_idx)->header.min_free_blocks = (ptr_page_head + pg_idx)->header.max_blocks;
		(ptr_page_head + pg_idx)->header.next_free_block_idx = 0;
		memset((ptr_page_head + pg_idx)->header.block_flags, BLOCK_OCCUPIED, sizeof((ptr_page_head + pg_idx)->header.block_flags));
		memset((ptr_page_head + pg_idx)->header.block_flags, BLOCK_EMPTY, ((ptr_page_head + pg_idx)->header.max_blocks));

		INIT_LIST_HEAD(&((ptr_page_head + pg_idx)->header.node));

		init_wf_queue_node(&((ptr_page_head + pg_idx)->header.wf_node));
		if (pg_idx < (n-1)) {
			(ptr_page_head + pg_idx)->header.wf_node.next = &((ptr_page_head + pg_idx + 1)->header.wf_node);
		}
	}

	LOG_EPILOG();
	return ptr_page_head;
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
	LOG_PROLOG();

	void* block = NULL;
	int block_idx = find_first_empty_block(ptr);
	if (block_idx != -1) {
		ptr->header.block_flags[block_idx] = BLOCK_OCCUPIED;
		uint32_t block_size = ptr->header.block_size + sizeof(mem_block_header_t);
		uint32_t byte_offset = sizeof(page_header_t) + (block_idx * block_size);
		block = (char*) ptr + byte_offset;

		mem_block_header_t *block_header = block;
		block_header->byte_offset = byte_offset;
		block_header->idx = block_idx;

		block = (char*)block + sizeof(mem_block_header_t);

		ptr->header.min_free_blocks -= 1;
	}

	LOG_EPILOG();
	return block;
}

void free_block(void *block_ptr) {
	LOG_PROLOG();

	mem_block_header_t *block_header = (mem_block_header_t*)((char*)block_ptr - sizeof(mem_block_header_t));
	page_t* page_ptr = (page_t*)((char*)block_header - block_header->byte_offset);
	page_ptr->header.block_flags[block_header->idx] = BLOCK_EMPTY;

	LOG_EPILOG();
}
