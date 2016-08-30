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
#include "logger.h"
#include <stdint.h>

typedef struct local_thread_data {
	list_t* bins[MAX_BINS];
	uint32_t min_blocks_bin[MAX_BINS];
	list_t* overflow[MAX_BINS];
	uint32_t count_overflow_blocks[MAX_BINS];
} local_thread_data_t;

typedef struct local_pool {
	int count_threads;
	int count_processors;
	int* last_shared_pool_idx; // Each threads keeps track of last used shared pool/queue and then fetches next in round robin fashion.
	local_thread_data_t* thread_data;
} local_pool_t;

local_pool_t* create_local_pool(int count_threads);

void* get_mem(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, uint32_t block_size);

void add_mem(local_pool_t *pool, shared_pool_t *shared_pool, void* mem, int thread_id);

#endif /* INCLUDES_LOCAL_POOL_H_ */
