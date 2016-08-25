/*
 * page_heap.h
 *
 *  Created on: Jul 27, 2016
 *      Author: ashish
 */

#ifndef INCLUDES_PAGE_HEAP_H_
#define INCLUDES_PAGE_HEAP_H_

#include "queue.h"
#include "config.h"
#include "page.h"
#include "logger.h"
#include <stdint.h>

#define COUNT_PAGE_HEAP_BINS 256

typedef struct page_heap {
	int count_threads;
	wf_queue_head_t* heap_data[COUNT_PAGE_HEAP_BINS];
	wf_queue_op_head_t* op_desc[COUNT_PAGE_HEAP_BINS];
	uint32_t* num_alloc_nodes;
	/* For stats*/
	uint32_t* count_malloc;
} page_heap_t;

page_heap_t* create_page_heap(int count_threads);

mem_block_header_t* get_n_pages_cont(page_heap_t* heap, uint32_t n, int thread_id);

void* get_n_pages(page_heap_t* heap, uint32_t n, int thread_id);

void add_to_page_heap(page_heap_t* ph, mem_block_header_t* mem, int thread_id);

#endif /* INCLUDES_PAGE_HEAP_H_ */
