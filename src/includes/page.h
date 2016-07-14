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
	uint32_t max_blocks; // no of blocks in page
	//uint32_t min_free_blocks; // no of available blocks in page (underestimate due to pd-cr)
	int next_free_block_idx;
	list_t node;  // used in mlfq in local-pool
	wf_queue_node_t wf_node;  // used in wait-free queue in shared-pool
	uint8_t block_flags[0];
};

typedef union page {
	page_header_t header;
	uint8_t memory[PAGE_SIZE];
} page_t;


page_t* create_npages_aligned(uint32_t block_size, uint32_t n);

//int find_first_empty_block(page_t* ptr);

// returns number of available blocks in page
int count_empty_blocks(page_t* ptr);

//uint32_t get_min_free_blocks(page_t* ptr);

// returns the number of blocks in a page
uint32_t get_max_blocks(page_t* ptr);

//bool has_empty_block(page_t* ptr);

void* malloc_block(page_t* ptr);

void free_block(void *block_ptr);

int is_page_aligned(void* page_ptr);

#endif /* INCLUDES_PAGE_H_ */
