/*
 * local_pool.h
 *
 *  Created on: Dec 28, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_LOCAL_POOL_H_
#define INCLUDES_LOCAL_POOL_H_

#include "list.h"
#include "config.h"
#include "page.h"
#include "shared_pool.h"
#include <stdint.h>

typedef struct local_thread_data {
	list_t bins[MAX_BINS][MAX_MLFQ]; // Every bin has a multi level feedback queue structure.
	uint32_t count_pages_in_queue[MAX_BINS][MAX_MLFQ];
	uint32_t counter_scan[MAX_BINS];
} local_thread_data_t;

typedef struct local_pool {
	int count_threads;
	int count_processors;
	int min_pages_per_bin;
	int* last_shared_pool_idx; // Each threads keeps track of last used shared pool/queue and then fetches next in round robin fashion.
	local_thread_data_t* thread_data;
} local_pool_t;

local_pool_t* create_local_pool(int count_threads);

int add_page(local_pool_t *pool, page_t *page, int thread_id);

void* malloc_block_from_pool(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, int block_size);

void local_pool_stats(local_pool_t *pool);

#endif /* INCLUDES_LOCAL_POOL_H_ */
