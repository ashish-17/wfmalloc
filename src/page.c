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
	for (block_idx = 0; block_idx < ptr->header.max_blocks; ++block_idx) {
		if (ptr->header.block_flags[block_idx] == BLOCK_EMPTY) {
			result = block_idx;
			break;
		}
	}

	LOG_EPILOG();
	return result;
}

int count_empty_blocks(page_t* ptr) {
	LOG_PROLOG();

	int count = 0;

	int block_idx = 0;
	for (block_idx = 0; block_idx < ptr->header.max_blocks; ++block_idx) {
		if (ptr->header.block_flags[block_idx] == BLOCK_EMPTY) {
			count++;
		}
	}

	LOG_EPILOG();
	return count;
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
