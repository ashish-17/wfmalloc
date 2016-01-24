/*
 * shared_pool.c
 *
 *  Created on: Jan 12, 2016
 *      Author: ashish
 */


#include "includes/shared_pool.h"
#include "includes/logger.h"
#include "includes/utils.h"
#include <unistd.h>
#include <stdlib.h>


shared_pool_t* create_shared_pool(int count_threads) {
	LOG_PROLOG();

	int num_processors = sysconf( _SC_NPROCESSORS_ONLN);

	shared_pool_t* pool = (shared_pool_t*)malloc(sizeof(shared_pool_t));
	pool->count_processors = num_processors;
	pool->count_threads = count_threads;
	pool->op_desc = create_queue_op_desc(count_threads);
	pool->thread_data = (shared_thread_data_t*)malloc(num_processors * sizeof(shared_thread_data_t));
	int bin = 0;
	int queue = 0;
	int pages = 0;
	int min_pages_per_bin = MIN_PAGES_PER_BIN(count_threads);
	page_t* ptr_page = NULL;
	for (queue = 0; queue < num_processors; ++queue) {
		int block_size = MIN_BLOCK_SIZE;
		for (bin = 0; bin < MAX_BINS; ++bin) {
			for (pages = 0; pages < (min_pages_per_bin + 1); ++pages) {
				ptr_page = create_page(block_size);
				init_wf_queue_node(&(ptr_page->header.wf_node));

				if (pages == 0) {
					pool->thread_data[queue].bins[bin] = create_wf_queue(&(ptr_page->header.wf_node));
				} else {
					wf_enqueue(pool->thread_data[queue].bins[bin], &(ptr_page->header.wf_node), pool->op_desc, 0);
				}
			}

			block_size *= 2;
		}
	}

	LOG_EPILOG();
	return pool;
}

void add_page_shared_pool(shared_pool_t *pool, page_t *page, int thread_id, int queue_idx) {
	LOG_PROLOG();

	int bin_idx = quick_log2(page->header.block_size) - quick_log2(MIN_BLOCK_SIZE);
	wf_enqueue(pool->thread_data[queue_idx].bins[bin_idx], &(page->header.wf_node), pool->op_desc, thread_id);

	LOG_EPILOG();
}

page_t* get_page_shared_pool(shared_pool_t *pool, int thread_id, int queue_idx, int block_size) {
	LOG_PROLOG();

	page_t* ret = NULL;

	int bin_idx = 0;
	if (block_size > MIN_BLOCK_SIZE) {
		bin_idx = quick_log2(upper_power_of_two(block_size)) - quick_log2(MIN_BLOCK_SIZE);
	}

	wf_queue_node_t* tmp = wf_dequeue(pool->thread_data[queue_idx].bins[bin_idx], pool->op_desc, thread_id);
	if (likely(tmp != NULL)) {
		ret = (page_t*)list_entry(tmp, page_header_t, wf_node);
	} else {
		ret = create_page(quick_pow2(quick_log2(MIN_BLOCK_SIZE) + bin_idx));
	}

	LOG_EPILOG();
	return ret;
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
