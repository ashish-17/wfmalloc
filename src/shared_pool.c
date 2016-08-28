/*
 * shared_pool.c
 *
 *  Created on: Jan 12, 2016
 *      Author: ashish
 */


#include "includes/shared_pool.h"
#include "includes/logger.h"
#include "includes/utils.h"
#include "includes/local_pool.h"
#include <unistd.h>
#include <stdlib.h>

uint32_t map_bin_to_size(uint32_t bin);
uint32_t map_size_to_bin(uint32_t size);

shared_pool_t* create_shared_pool(int count_threads) {
	LOG_PROLOG();

	int num_processors = sysconf( _SC_NPROCESSORS_ONLN);

	shared_pool_t* pool = (shared_pool_t*)malloc(sizeof(shared_pool_t));
	pool->count_processors = num_processors;
	pool->count_threads = count_threads;
	pool->ph = create_page_heap(count_threads);
	pool->thread_data = (shared_thread_data_t*)malloc(num_processors * sizeof(shared_thread_data_t));
	int bin = 0;
	int queue = 0;
	int pages = 0;
	page_t* ptr_page = NULL;
	mem_block_header_t* mem = NULL;
	for (queue = 0; queue < num_processors; ++queue) {
		for (bin = 0; bin < MAX_BINS; ++bin) {
			mem = (mem_block_header_t*)malloc(sizeof(mem_block_header_t) + map_bin_to_size(bin));
			pool->thread_data[queue].bins[bin] = create_wf_queue(&(mem->wf_node));
			pool->thread_data[queue].op_desc[bin] = create_queue_op_desc(count_threads);
			pool->thread_data[queue].count_ops[bin] = 0;
			pool->thread_data[queue].count_pages_alloc[bin] = 1;
		}
	}

	LOG_EPILOG();
	return pool;
}

void add_mem_shared_pool(shared_pool_t *pool, mem_block_header_t *mem, int thread_id, int queue_idx) {
	LOG_PROLOG();

	uint32_t bin_idx = map_size_to_bin(mem->size);
	wf_enqueue(pool->thread_data[queue_idx].bins[bin_idx], &(mem->wf_node), pool->thread_data[queue_idx].op_desc[bin_idx], thread_id);

	LOG_EPILOG();
}

mem_block_header_t* get_mem_shared_pool(shared_pool_t *pool, int thread_id, int queue_idx, int block_size) {
	mem_block_header_t* mem = NULL;
	LOG_PROLOG();

	page_t* ret = NULL;

	int bin_idx = 0;
	if (block_size > MIN_BLOCK_SIZE) {
		bin_idx = map_size_to_bin(block_size);
	}

	wf_queue_node_t* tmp = wf_dequeue(pool->thread_data[queue_idx].bins[bin_idx], pool->thread_data[queue_idx].op_desc[bin_idx], thread_id);
	if (likely(tmp != NULL)) {
		mem = (mem_block_header_t*)list_entry(tmp, mem_block_header_t, wf_node);
	} else {
		mem_block_header_t* mem_alloc = get_n_pages_cont(pool->ph, pool->thread_data[queue_idx].count_pages_alloc[bin_idx], thread_id);
		pool->thread_data[queue_idx].count_ops[bin_idx]++;

		unsigned int log2 = quick_log2(pool->thread_data[queue_idx].count_ops[bin_idx]);
		if (unlikely(log2 != -1)) {
			pool->thread_data[queue_idx].count_pages_alloc[bin_idx] = log2;
		}

		uint32_t total_mem = (mem->size / PAGE_SIZE)*sizeof(mem_block_header_t) + mem->size;
		uint32_t mem_blk_offset = 0;
		mem_block_header_t* mem_extra_queue = NULL;
		mem_block_header_t* prev_queue = NULL;
		for (mem_blk_offset = 0; mem_blk_offset < total_mem;) {
			if (unlikely(mem_blk_offset == 0)) {
				mem = (mem_block_header_t*)mem_alloc;
				mem->size = block_size;
				init_wf_queue_node(&(mem->wf_node));
			} else if (unlikely(mem_extra_queue == NULL)) {
				mem_extra_queue = (mem_block_header_t*)mem_alloc;
				mem_extra_queue->size = block_size;
				init_wf_queue_node(&(mem_extra_queue->wf_node));
				prev_queue = mem_extra_queue;
			} else {
				mem_alloc->size = block_size;
				init_wf_queue_node(&(mem_alloc->wf_node));
				prev_queue->wf_node.next = &(((mem_block_header_t*)mem_alloc)->wf_node);
				prev_queue = mem_alloc;
			}

			mem_blk_offset += sizeof(mem_block_header_t) + block_size;
			mem_alloc = (mem_block_header_t*)((char*)mem_alloc + mem_blk_offset);
		}

		add_mem_shared_pool(pool, mem_extra_queue, thread_id, queue_idx);
	}

	LOG_EPILOG();
	return mem;
}

void shared_pool_stats(shared_pool_t *pool) {
	LOG_PROLOG();

	LOG_DEBUG("########--SHARED POOL--########");
	LOG_DEBUG("Number of threads = %d", pool->count_threads);
	LOG_DEBUG("Number of processors = %d", pool->count_processors);

	int queue_idx = 0, bin = 0;
	shared_thread_data_t* thread_data = NULL;
	int count_pages = 0;
	for (queue_idx = 0; queue_idx < pool->count_processors; ++queue_idx) {
		LOG_DEBUG("\tQueue %d", queue_idx);
		thread_data = (pool->thread_data + queue_idx);
		for (bin = 0; bin < MAX_BINS; ++bin) {
			LOG_DEBUG("\t\tBin %d", bin);
			count_pages = wf_queue_count_nodes(thread_data->bins[bin]);

			LOG_DEBUG("\t\t\tPage %d", count_pages);
		}
	}

	LOG_EPILOG();
}


uint32_t map_bin_to_size(uint32_t bin) {
	uint32_t size = 0;
	LOG_PROLOG();

	size = quick_pow2(bin + 2);

	LOG_EPILOG();
	return size;
}

uint32_t map_size_to_bin(uint32_t size) {
	uint32_t bin = 0;
	LOG_PROLOG();

	bin = quick_log2(size) - 2;

	LOG_EPILOG();
	return bin;
}
