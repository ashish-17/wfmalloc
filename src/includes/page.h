/*
 * page.h
 *
 *  Created on: Dec 26, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_PAGE_H_
#define INCLUDES_PAGE_H_


#include "config.h"
#include <stdint.h>
#include <stdbool.h>
#include "list.h"
#include "queue.h"

typedef struct page_header page_header_t;

typedef struct mem_block_header {
	uint32_t byte_offset; // Location of header relative to block pointer.
	uint32_t idx;
} mem_block_header_t;

struct page_header {
	uint32_t block_size;
	uint32_t max_blocks;
	uint32_t min_free_blocks;
	list_t node;
	wf_queue_node_t wf_node;
	uint8_t block_flags[MAX_BLOCKS_IN_PAGE];
};

typedef union page {
	page_header_t header;
	uint8_t memory[PAGE_SIZE];
} page_t;

page_t* create_page(uint32_t block_size);

int find_first_empty_block(page_t* ptr);

int count_empty_blocks(page_t* ptr);

uint32_t get_min_free_blocks(page_t* ptr);

bool has_empty_block(page_t* ptr);

void* malloc_block(page_t* ptr);

void free_block(void *block_ptr);

#endif /* INCLUDES_PAGE_H_ */
