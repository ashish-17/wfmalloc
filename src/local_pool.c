/*
 * local_pool.c
 *
 *  Created on: Dec 28, 2015
 *      Author: ashish
 */

#include "includes/local_pool.h"
#include "includes/utils.h"
#include "includes/logger.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

local_pool_t* create_local_pool(int count_threads) {
	LOG_PROLOG();

	int num_processors = sysconf( _SC_NPROCESSORS_ONLN);

	local_pool_t *pool = (local_pool_t*) malloc(sizeof(local_pool_t));
	pool->count_processors = num_processors;
	pool->count_threads = count_threads;
	pool->last_shared_pool_idx = (int*)malloc(sizeof(int) * count_threads);
	pool->thread_data = (local_thread_data_t*)malloc(sizeof(local_thread_data_t) * count_threads);

	int thread = 0, bin = 0, mlfq = 0, page_idx = 0;
	local_thread_data_t* thread_data = NULL;
	page_t* ptr_page = NULL;
	for (thread = 0; thread < count_threads; ++thread) {
		*(pool->last_shared_pool_idx + thread) = (thread % num_processors);
		thread_data = (pool->thread_data + thread);
		for (bin = 0; bin < MAX_BINS; ++bin) {
			thread_data->bins[bin] = NULL;
			thread_data->min_blocks_bin[bin] = 0;

			thread_data->overflow[bin] = NULL;
			thread_data->count_overflow_blocks[bin] = 0;
		}
	}

	LOG_EPILOG();
	return pool;
}


void* get_mem(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, uint32_t block_size) {
	LOG_PROLOG();
	uint32_t bin = map_size_to_bin(block_size);
	mem_block_header_t* mem = NULL;
	if (pool->thread_data[thread_id].count_overflow_blocks[bin] > 0) {
		list_t* block = pool->thread_data[thread_id].overflow[bin];
		pool->thread_data[thread_id].overflow[bin] = pool->thread_data[thread_id].overflow[bin]->next;
		pool->thread_data[thread_id].count_overflow_blocks[bin]--;
		mem = (mem_block_header_t*)list_entry(block, mem_block_header_t, node);
	} else if (pool->thread_data[thread_id].bins[bin] != NULL) {
		list_t* block = pool->thread_data[thread_id].bins[bin];
		pool->thread_data[thread_id].bins[bin] = pool->thread_data[thread_id].bins[bin]->next;
		mem = (mem_block_header_t*)list_entry(block, mem_block_header_t, node);
		pool->thread_data[thread_id]->min_blocks_bin[bin]--;
	} else {
		mem_block_header_t* new_mem =  get_mem_shared_pool(shared_pool, thread_id, *(pool->last_shared_pool_idx + thread_id), block_size);
		pool->thread_data[thread_id].bins[bin] = &(new_mem->node);
		pool->thread_data[thread_id]->min_blocks_bin[bin] = MAX_BLOCKS_RUN;

		list_t* block = pool->thread_data[thread_id].bins[bin];
		pool->thread_data[thread_id].bins[bin] = pool->thread_data[thread_id].bins[bin]->next;
		pool->thread_data[thread_id]->min_blocks_bin[bin]--;

		mem = (mem_block_header_t*)list_entry(block, mem_block_header_t, node);

		*(pool->last_shared_pool_idx + thread_id) = (*(pool->last_shared_pool_idx + thread_id) + 1) % pool->count_processors;
	}

	void* ret_mem = (char*)mem + sizeof(mem_block_header_t);
	LOG_EPILOG();
	return ret_mem;
}


void add_mem(local_pool_t *pool, shared_pool_t *shared_pool, void* mem, int thread_id) {
	LOG_PROLOG();

	mem_block_header_t* mem_block = (char*)mem - sizeof(mem_block_header_t);
	uint32_t bin = map_size_to_bin(mem_block->size);
	if (pool->thread_data[thread_id]->min_blocks_bin[bin] > THRESHOLD_OVERFLOW_BIN) {
		if (pool->thread_data[thread_id]->count_overflow_blocks == MAX_BLOCKS_RUN) {
			list_t* block = pool->thread_data[thread_id].overflow[bin];
			mem_block_header_t* mem_overflow = (mem_block_header_t*)list_entry(block, mem_block_header_t, node);
			add_mem_shared_pool(shared_pool, mem_overflow, thread_id, *(pool->last_shared_pool_idx + thread_id));

			*(pool->last_shared_pool_idx + thread_id) = (*(pool->last_shared_pool_idx + thread_id) + 1) % pool->count_processors;
			pool->thread_data[thread_id].overflow[bin] = NULL;
			pool->thread_data[thread_id]->count_overflow_blocks = 0;
		}

		mem_block->node.next = pool->thread_data[thread_id].overflow[bin];
		pool->thread_data[thread_id].overflow[bin] = &(mem_block->node);
		pool->thread_data[thread_id]->count_overflow_blocks[bin]++;
	} else {
		mem_block->node.next = pool->thread_data[thread_id].bins[bin];
		pool->thread_data[thread_id].bins[bin] = &(mem_block->node);
		pool->thread_data[thread_id]->min_blocks_bin[bin]++;
	}
	LOG_EPILOG();
}
