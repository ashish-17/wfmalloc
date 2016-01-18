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
	int block_size = MIN_BLOCK_SIZE;
	page_t* ptr_page = NULL;
	for (thread = 0; thread < count_threads; ++thread) {
		*(pool->last_shared_pool_idx + thread) = (thread % num_processors);
		thread_data = (pool->thread_data + thread);
		block_size = MIN_BLOCK_SIZE;
		for (bin = 0; bin < MAX_BINS; ++bin) {
			for (mlfq = 0; mlfq < MAX_MLFQ; ++mlfq) {
				INIT_LIST_HEAD(&(thread_data->bins[bin][mlfq]));
			}

			for (page_idx = 0; page_idx < MIN_PAGES_PER_BIN; ++page_idx) {
				ptr_page = create_page(block_size);
				add_page(pool, ptr_page, thread);
			}

			block_size *= 2;
		}
	}

	LOG_EPILOG();
	return pool;
}

int add_page(local_pool_t *pool, page_t *page, int thread_id) {
	LOG_PROLOG();

	int bin_idx = quick_log2(page->header.block_size) - quick_log2(MIN_BLOCK_SIZE);
	int empty = count_empty_blocks(page);
	int mlfq = CALC_MLFQ_IDX(empty, page->header.max_blocks);

	list_add_tail(&(page->header.node), &((pool->thread_data + thread_id)->bins[bin_idx][mlfq]));

	LOG_EPILOG();
	return empty;
}

void* malloc_block_from_pool(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, int block_size) {
	LOG_PROLOG();

	void* block = NULL;
	int bin_idx = 0;
	if (block_size > MIN_BLOCK_SIZE) {
		bin_idx = quick_log2(upper_power_of_two(block_size)) - quick_log2(MIN_BLOCK_SIZE);
	}

	local_thread_data_t* thread_data = (pool->thread_data + thread_id);
	list_t* tmp, *swap_tmp = NULL;
	list_t* ptr_page_node = NULL;
	list_t to_be_removed, to_be_updated;
	INIT_LIST_HEAD(&to_be_removed);
	INIT_LIST_HEAD(&to_be_updated);

	int mlfq = 0;
	int count_pages = 0;
	for (mlfq = 0; mlfq < MAX_MLFQ  && (ptr_page_node == NULL); ++mlfq) {
		count_pages = 0;

		tmp = (&(thread_data->bins[bin_idx][mlfq]))->next;
		while (tmp != (&(thread_data->bins[bin_idx][mlfq]))) {
			count_pages++;

			if (mlfq < (MAX_MLFQ - 1)) {
				if (ptr_page_node == NULL) {
					ptr_page_node = tmp;

					swap_tmp = tmp->next;
					list_del(tmp);
					list_add(tmp, &to_be_updated);
					tmp = swap_tmp;
					break;//continue;
				}
			}

			if (mlfq < MLFQ_THRESHOLD) {
				if (count_pages > MIN_PAGES_PER_BIN) {
					swap_tmp = tmp->next;
					/*list_del(tmp);
					list_add(tmp, &to_be_removed);*/
					tmp = swap_tmp;
				} else {
					tmp = tmp->next;
				}
			} else if (mlfq < (MAX_MLFQ - 1)) {
				break;
			} else {
				swap_tmp = tmp->next;
				list_del(tmp);
				list_add(tmp, &to_be_updated);
				tmp = swap_tmp;
			}
		}

		tmp = to_be_removed.next;
		while(tmp != &to_be_removed) {
			swap_tmp = tmp->next;
			list_del(tmp);
			*(pool->last_shared_pool_idx + thread_id) = (*(pool->last_shared_pool_idx + thread_id) + 1) % pool->count_processors;
			add_page_shared_pool(shared_pool, (page_t*)list_entry(tmp, page_header_t, node), thread_id, *(pool->last_shared_pool_idx + thread_id));
			tmp = swap_tmp;
		}

		tmp = to_be_updated.next;
		while(tmp != &to_be_updated) {
			swap_tmp = tmp->next;
			list_del(tmp);
			if (add_page(pool, (page_t*) list_entry(tmp, page_header_t, node), thread_id) > 0 && (ptr_page_node == NULL)) {
				ptr_page_node = tmp;
			}
			tmp = swap_tmp;
		}

		if (ptr_page_node != NULL) {
			break;
		}
	}

	if (ptr_page_node == NULL) {
		*(pool->last_shared_pool_idx + thread_id) = (*(pool->last_shared_pool_idx + thread_id) + 1) % pool->count_processors;
		ptr_page_node = &(get_page_shared_pool(shared_pool, thread_id, *(pool->last_shared_pool_idx + thread_id), block_size)->header.node);
		add_page(pool, (page_t*) list_entry(ptr_page_node, page_header_t, node), thread_id);
	}

	if (ptr_page_node != NULL) {
		block = malloc_block((page_t*) list_entry(ptr_page_node, page_header_t, node));
	}

	LOG_EPILOG();
	return block;
}

void local_pool_stats(local_pool_t *pool) {
	LOG_PROLOG();

	LOG_INFO("########--LOCAL POOL--########");
	LOG_INFO("Number of threads = %d", pool->count_threads);
	LOG_INFO("Number of processors = %d", pool->count_processors);

	int thread = 0, bin = 0, mlfq = 0;
	local_thread_data_t* thread_data = NULL;
	int count_pages = 0;
	list_t* tmp = NULL;
	for (thread = 0; thread < pool->count_threads; ++thread) {
		LOG_INFO("\tThread %d", thread);
		LOG_INFO("\t\tLast used shared pool idx %d", *(pool->last_shared_pool_idx + thread));
		thread_data = (pool->thread_data + thread);
		for (bin = 0; bin < MAX_BINS; ++bin) {
			LOG_INFO("\t\tBin %d", bin);
			for (mlfq = 0; mlfq < MAX_MLFQ; ++mlfq) {
				count_pages = 0;
				LOG_INFO("\t\t\tMLFQ %d", mlfq);
				list_for_each(tmp, &(thread_data->bins[bin][mlfq])) {

					LOG_INFO("\t\t\t\tPage %d - Blocks = %d", count_pages, count_empty_blocks((page_t*) list_entry(tmp, page_header_t, node)));
					count_pages++;
				}

				LOG_INFO("\t\t\tMLFQ %d has %d pages", mlfq, count_pages);
			}
		}
	}

	LOG_EPILOG();
}
