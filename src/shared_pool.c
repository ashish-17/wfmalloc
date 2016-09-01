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

mem_run_t* alloc_mem_run(shared_pool_t *pool, uint32_t count_blocks, uint32_t block_size, int thread_id);

shared_pool_t* create_shared_pool(int count_threads) {
	LOG_PROLOG();

	int num_processors = sysconf( _SC_NPROCESSORS_ONLN);

	shared_pool_t* pool = (shared_pool_t*)malloc(sizeof(shared_pool_t));
	pool->count_processors = num_processors;
	pool->count_threads = count_threads;
	pool->ph = create_page_heap(count_threads);
	pool->thread_data = (shared_thread_data_t*)malloc(num_processors * sizeof(shared_thread_data_t));

	mem_run_t* tmp_mem_run = (mem_run_t*)malloc(sizeof(mem_run_t));
	tmp_mem_run->list_blocks = NULL;
	init_wf_queue_node(&(tmp_mem_run->wf_node));

	pool->empty_runs = create_wf_queue(&(tmp_mem_run->wf_node));
	pool->op_desc_runs = create_queue_op_desc(count_threads);

	int bin = 0;
	int queue = 0;
	for (queue = 0; queue < num_processors; ++queue) {
		for (bin = 0; bin < MAX_BINS; ++bin) {
			tmp_mem_run = alloc_mem_run(pool, MAX_BLOCKS_RUN, map_bin_to_size(bin), 0);
			pool->thread_data[queue].bins[bin] = create_wf_queue(&(tmp_mem_run->wf_node));
			pool->thread_data[queue].op_desc[bin] = create_queue_op_desc(count_threads);
			pool->thread_data[queue].count_ops[bin] = 0;
		}
	}

	LOG_EPILOG();
	return pool;
}

void add_mem_shared_pool(shared_pool_t *pool, mem_block_header_t *mem, int thread_id, int queue_idx) {
	LOG_PROLOG();

	wf_queue_node_t* tmp = wf_dequeue(pool->empty_runs, pool->op_desc_runs, thread_id);
	if (tmp != NULL) {
		mem_run_t* run = (mem_run_t*)list_entry(tmp, mem_run_t, wf_node);
		run->list_blocks = mem;

		uint32_t bin_idx = map_size_to_bin(mem->size);
		init_wf_queue_node(&(run->wf_node));
		wf_enqueue(pool->thread_data[queue_idx].bins[bin_idx], &(run->wf_node), pool->thread_data[queue_idx].op_desc[bin_idx], thread_id);
	} else {
		LOG_ERROR("Insufficient runs");
	}

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

	mem_run_t* mem_run = NULL;
	wf_queue_node_t* tmp = wf_dequeue(pool->thread_data[queue_idx].bins[bin_idx], pool->thread_data[queue_idx].op_desc[bin_idx], thread_id);
	if (likely(tmp != NULL)) {
		mem_run = (mem_run_t*)list_entry(tmp, mem_run_t, wf_node);
	} else {
		mem_run = alloc_mem_run(pool, MAX_BLOCKS_RUN, block_size, 0);
	}

	mem = mem_run->list_blocks;
	mem_run = (mem_run_t*)malloc(sizeof(mem_run_t));
	mem_run->list_blocks = NULL;
	init_wf_queue_node(&(mem_run->wf_node));
	wf_enqueue(pool->empty_runs, &(mem_run->wf_node), pool->op_desc_runs, thread_id);

	LOG_EPILOG();
	return mem;
}

void* get_big_mem(shared_pool_t *pool, int thread_id, int block_size) {
	mem_block_header_t* mem = NULL;
	LOG_PROLOG();

	uint32_t pages_required = (block_size / PAGE_SIZE) +  (block_size % PAGE_SIZE == 0 ? 0 : 1);
	mem = get_n_pages_cont(pool->ph, pages_required, thread_id);

	void* ret_mem = (char*)mem + sizeof(mem_block_header_t);
	LOG_EPILOG();
	return ret_mem;
}


void free_big_mem(shared_pool_t *pool, mem_block_header_t *mem, int thread_id) {
	LOG_PROLOG();
	add_to_page_heap(pool->ph, mem, thread_id);
	LOG_EPILOG();
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


mem_run_t* alloc_mem_run(shared_pool_t *pool, uint32_t count_blocks, uint32_t block_size, int thread_id) {
	mem_run_t* run = NULL;
	LOG_PROLOG();
	uint32_t pages_required = ((sizeof(mem_run_t) + count_blocks*(block_size + sizeof(mem_block_header_t))) / PAGE_SIZE) + 1;
	mem_block_header_t* mem_alloc = get_n_pages_cont(pool->ph, pages_required, thread_id);
	run = mem_alloc;

	uint32_t total_mem = sizeof(mem_block_header_t) + mem_alloc->size - sizeof(mem_run_t);
	mem_alloc = (mem_block_header_t*)((char*)mem_alloc + sizeof(mem_run_t));

	uint32_t mem_blk_offset = 0;

	mem_block_header_t* mem = NULL;
	mem_block_header_t* prev_queue = NULL;
	for (mem_blk_offset = 0; mem_blk_offset < total_mem;) {
		if (unlikely(mem_blk_offset == 0)) {
			mem = (mem_block_header_t*)mem_alloc;
			mem->size = block_size;
			mem->node.next = NULL;
			init_wf_queue_node(&(mem->wf_node));

			prev_queue = mem;
		} else {
			mem_alloc->size = block_size;
			init_wf_queue_node(&(mem_alloc->wf_node));
			mem_alloc->node.next = NULL;

			prev_queue->node.next = &(((mem_block_header_t*)mem_alloc)->node);
			prev_queue = mem_alloc;
		}

		mem_blk_offset += sizeof(mem_block_header_t) + block_size;
		if (mem_blk_offset < total_mem) {
			mem_alloc = (mem_block_header_t*)((char*)mem_alloc + sizeof(mem_block_header_t) + block_size);
		}
	}

	run->list_blocks = mem;
	init_wf_queue_node(&(run->wf_node));

	LOG_EPILOG();
	return run;
}

