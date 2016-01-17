/*
 * shared_pool.c
 *
 *  Created on: Jan 12, 2016
 *      Author: ashish
 */


#include "includes/shared_pool.h"
#include "includes/logger.h"
#include <unistd.h>
#include <stdlib.h>


shared_pool_t* create_shared_pool(int count_threads) {
	LOG_PROLOG();

	int num_processors = sysconf( _SC_NPROCESSORS_ONLN);

	shared_pool_t* pool = (shared_pool_t*)malloc(sizeof(shared_pool_t));
	pool->count_processors = num_processors;
	pool->count_threads = count_threads;
	pool->op_desc = create_queue_op_desc(count_threads);
	pool->shared_thread_data = (shared_thread_data_t*)malloc(num_processors * sizeof(shared_thread_data_t));
	int bin = 0;
	int queue = 0;
	int pages = 0;
	page_t* ptr_page = NULL;
	for (queue = 0; queue < num_processors; ++queue) {
		int block_size = MIN_BLOCK_SIZE;
		for (bin = 0; bin < MAX_BINS; ++bin) {
			for (pages = 0; pages < (MIN_PAGES_PER_BIN + 1); ++pages) {
				ptr_page = create_page(block_size);
				init_wf_queue_node(&(ptr_page->header.wf_node));

				if (pages == 0) {
					pool->shared_thread_data[queue].bins[bin] = create_wf_queue(&(ptr_page->header.wf_node));
				} else {
					wf_enqueue(pool->shared_thread_data[queue].bins[bin], &(ptr_page->header.wf_node), pool->op_desc, 0);
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
	wf_enqueue(pool->shared_thread_data[queue_idx].bins[bin_idx], &(page->header.wf_node), pool->op_desc, thread_id);

	LOG_EPILOG();
}

page_t* get_page_shared_pool(shared_pool_t *pool, int thread_id, int queue_idx, int block_size) {
	LOG_PROLOG();

	page_t* ret = NULL;

	int bin_idx = 0;
	if (block_size > MIN_BLOCK_SIZE) {
		bin_idx = quick_log2(upper_power_of_two(block_size)) - quick_log2(MIN_BLOCK_SIZE);
	}

	ret = wf_dequeue(pool->shared_thread_data[queue_idx].bins[bin_idx], pool->op_desc, thread_id);

	LOG_EPILOG();
	return ret;
}
