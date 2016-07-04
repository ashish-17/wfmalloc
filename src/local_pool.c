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

#include <assert.h>

local_pool_t* create_local_pool(int count_threads) {
	LOG_PROLOG();

	int num_processors = sysconf( _SC_NPROCESSORS_ONLN);

	local_pool_t *pool = (local_pool_t*) malloc(sizeof(local_pool_t));
	pool->count_processors = num_processors;
	pool->count_threads = count_threads;
	pool->min_pages_per_bin = MIN_PAGES_PER_BIN(count_threads);
	//LOG_INFO("min_pages_per_bin = %d", pool->min_pages_per_bin);

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
				thread_data->count_pages_in_queue[bin][mlfq] = 0;
			}

			for (page_idx = 0; page_idx < pool->min_pages_per_bin; ++page_idx) {
				ptr_page = create_page_aligned(block_size);
				add_page(pool, ptr_page, thread);
			}

			block_size *= 2;
			thread_data->page_cache[bin] = NULL;
		}


		#ifdef LOG_LEVEL_STATS
			thread_data->count_request_shared_pool = 0;
			thread_data->count_back2_shared_pool = 0;
			thread_data->count_mlfq_change = 0;
			thread_data->count_malloc = 0;
			thread_data->time_malloc_shared_pool = 0;
			thread_data->time_malloc_total = 0;
			thread_data->time_malloc_system_call = 0;
		#endif
	}

	LOG_EPILOG();
	return pool;
}

void add_page_to_mlfq(local_pool_t *pool, page_t *page, int thread_id, int mlfq) {
	LOG_PROLOG();

	int bin_idx = quick_log2(page->header.block_size) - quick_log2(MIN_BLOCK_SIZE);

	list_add_tail(&(page->header.node), &((pool->thread_data + thread_id)->bins[bin_idx][mlfq]));
	(pool->thread_data + thread_id)->count_pages_in_queue[bin_idx][mlfq] += 1;

#ifdef LOG_LEVEL_STATS
	(pool->thread_data + thread_id)->count_mlfq_change++;
#endif
	LOG_EPILOG();
}

int add_page(local_pool_t *pool, page_t *page, int thread_id) {
	LOG_PROLOG();

	int empty = count_empty_blocks(page);
	int mlfq = CALC_MLFQ_IDX(empty, page->header.max_blocks);
	if (unlikely(mlfq < 0 || mlfq > MAX_MLFQ)) {
		LOG_WARN("Invalid Queue index(%d) using empty = %d, max = %d", mlfq, empty, page->header.max_blocks);
	}

	if (unlikely(mlfq == MAX_MLFQ-1 && empty != 0)) {
		LOG_WARN("Something went wrong! - empty = %d, max = %d", empty, page->header.max_blocks);
	}

	if (unlikely(mlfq != MAX_MLFQ-1 && empty == 0)) {
		LOG_WARN("Something realy!!!!! went wrong! - empty = %d, max = %d", empty, page->header.max_blocks);
	}

#ifdef LOG_LEVEL_STATS
	(pool->thread_data + thread_id)->count_blocks_counting_ops++;
#endif

	add_page_to_mlfq(pool, page, thread_id, mlfq);

	LOG_EPILOG();
	return mlfq;
}

void* malloc_block_from_pool(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, int block_size) {
	LOG_PROLOG();

#ifdef LOG_LEVEL_STATS
	struct timeval s, e;
	gettimeofday(&s, NULL);
#endif

	void* block = NULL;
	int bin_idx = 0;
	if (block_size > MIN_BLOCK_SIZE) {
		bin_idx = quick_log2(upper_power_of_two(block_size)) - quick_log2(MIN_BLOCK_SIZE);
	}

	local_thread_data_t* thread_data = (pool->thread_data + thread_id);

	list_t* tmp, *swap_tmp = NULL;
	list_t* ptr_page_node = NULL;
	list_t to_be_removed, to_be_updated;
	page_t* tmp_page = NULL;
	INIT_LIST_HEAD(&to_be_removed);
	INIT_LIST_HEAD(&to_be_updated);

#ifdef LOG_LEVEL_STATS
	thread_data->count_malloc++;
#endif

	int mlfq = 0;
	int count_pages = 0;
	if (thread_data->page_cache[bin_idx] == NULL) {
		for (mlfq = 0; mlfq < MAX_MLFQ && (ptr_page_node == NULL); ++mlfq) {
			tmp = (thread_data->bins[bin_idx][mlfq]).next;
			if (likely(thread_data->count_pages_in_queue[bin_idx][mlfq] > 0)) {
				if (likely(mlfq < (MAX_MLFQ - 1))) {
					ptr_page_node = tmp;
					tmp_page = (page_t*) list_entry(tmp, page_header_t, node);

					swap_tmp = tmp->next;
					list_del(tmp);
					tmp = swap_tmp;

					thread_data->count_pages_in_queue[bin_idx][mlfq] -= 1;

					if (likely(tmp_page->header.min_free_blocks > 1)) {
						thread_data->page_cache[bin_idx] = tmp_page;
					} else {
						list_add(ptr_page_node, &to_be_updated);
					}

					if (likely(mlfq < MLFQ_THRESHOLD)) {
						count_pages =
								thread_data->count_pages_in_queue[bin_idx][0]
										+ thread_data->count_pages_in_queue[bin_idx][1]
										+ thread_data->count_pages_in_queue[bin_idx][2]
										+ thread_data->count_pages_in_queue[bin_idx][3];

						if ((count_pages > pool->min_pages_per_bin)
								&& (tmp != &(thread_data->bins[bin_idx][mlfq]))) {
							list_del(tmp);
							list_add(tmp, &to_be_removed);
							thread_data->count_pages_in_queue[bin_idx][mlfq] -= 1;
						}
					}
				} else {
					list_del(tmp);
					list_add(tmp, &to_be_updated);

					thread_data->count_pages_in_queue[bin_idx][mlfq] -= 1;
				}
			}
		}
	} else {
		ptr_page_node = &(thread_data->page_cache[bin_idx]->header.node);
		if (thread_data->page_cache[bin_idx]->header.min_free_blocks == 1) {
			thread_data->page_cache[bin_idx] = NULL;
			list_add(ptr_page_node, &to_be_updated);
		}
	}

	if (likely(ptr_page_node != NULL)) {
		block = malloc_block((page_t*) list_entry(ptr_page_node, page_header_t, node));
#ifdef LOG_LEVEL_STATS
		//thread_data->count_blocks_counting_ops++;
#endif
	}

	int tmp_mlfq = -1;
	if (unlikely(list_empty(&to_be_updated) == 0)) {
		tmp = to_be_updated.next;
		while (tmp != &to_be_updated) {
			swap_tmp = tmp->next;
			list_del(tmp);
			tmp_mlfq = add_page(pool, (page_t*) list_entry(tmp, page_header_t, node), thread_id);

			if (tmp_mlfq < (MAX_MLFQ - 1) && (ptr_page_node == NULL)) {
				ptr_page_node = tmp;

				tmp_page = (page_t*) list_entry(ptr_page_node, page_header_t, node);
				block = malloc_block(tmp_page);
#ifdef LOG_LEVEL_STATS
		//thread_data->count_blocks_counting_ops++;
#endif
				if (tmp_page->header.min_free_blocks == 0) {
					list_del(ptr_page_node);
					thread_data->count_pages_in_queue[bin_idx][tmp_mlfq] -= 1;

					add_page_to_mlfq(pool, tmp_page, thread_id, (MAX_MLFQ - 1));
				}
			}

			tmp = swap_tmp;
		}
	}

	if (unlikely(ptr_page_node == NULL)) {
#ifdef LOG_LEVEL_STATS
	struct timeval ss, es;
	gettimeofday(&ss, NULL);
#endif
		*(pool->last_shared_pool_idx + thread_id) = (*(pool->last_shared_pool_idx + thread_id) + 1) % pool->count_processors;
		ptr_page_node = &(get_page_shared_pool(shared_pool, pool, thread_id, *(pool->last_shared_pool_idx + thread_id), block_size)->header.node);
		thread_data->page_cache[bin_idx] = (page_t*) list_entry(ptr_page_node, page_header_t, node);

#ifdef LOG_LEVEL_STATS
		thread_data->count_request_shared_pool++;
		gettimeofday(&es, NULL);
		long int timeTakenSharedMalloc = ((es.tv_sec * 1000000 + es.tv_usec) - (ss.tv_sec * 1000000 + ss.tv_usec ));
		thread_data->time_malloc_shared_pool += timeTakenSharedMalloc;
#endif
	}

	if (unlikely(block == NULL)) {
		tmp_page = (page_t*) list_entry(ptr_page_node, page_header_t, node);
		block = malloc_block(tmp_page);
#ifdef LOG_LEVEL_STATS
		//thread_data->count_blocks_counting_ops++;
#endif
	}

	if (unlikely(list_empty(&to_be_removed) == 0)) {
		tmp = to_be_removed.next;
		while(tmp != &to_be_removed) {
			swap_tmp = tmp->next;
			list_del(tmp);
			*(pool->last_shared_pool_idx + thread_id) = (*(pool->last_shared_pool_idx + thread_id) + 1) % pool->count_processors;
			add_page_shared_pool(shared_pool, (page_t*)list_entry(tmp, page_header_t, node), thread_id, *(pool->last_shared_pool_idx + thread_id));
			tmp = swap_tmp;

#ifdef LOG_LEVEL_STATS
			thread_data->count_back2_shared_pool++;
#endif
		}
	}

#ifdef LOG_LEVEL_STATS
	gettimeofday(&e, NULL);
	long int timeTakenMalloc = ((e.tv_sec * 1000000 + e.tv_usec) - (s.tv_sec * 1000000 + s.tv_usec ));
	thread_data->time_malloc_total += timeTakenMalloc;
#endif

	LOG_EPILOG();

	//assert(block); //No returning null pointers.

	return block;
}

void local_pool_stats(local_pool_t *pool) {
	LOG_PROLOG();

	LOG_DEBUG("########--LOCAL POOL--########");
	LOG_DEBUG("Number of threads = %d", pool->count_threads);
	LOG_INFO("Number of processors = %d", pool->count_processors);

	int thread = 0, bin = 0, mlfq = 0;
	local_thread_data_t* thread_data = NULL;
	int count_pages = 0;
	list_t* tmp = NULL;
	for (thread = 0; thread < pool->count_threads; ++thread) {
		LOG_DEBUG("\tThread %d", thread);
		LOG_DEBUG("\t\tLast used shared pool idx %d", *(pool->last_shared_pool_idx + thread));
		thread_data = (pool->thread_data + thread);
		for (bin = 0; bin < MAX_BINS; ++bin) {
			LOG_INFO("\t\tBin %d", bin);
			for (mlfq = 0; mlfq < MAX_MLFQ; ++mlfq) {
				count_pages = 0;
				LOG_DEBUG("\t\t\tMLFQ %d", mlfq);
				list_for_each(tmp, &(thread_data->bins[bin][mlfq])) {

					LOG_DEBUG("\t\t\t\tPage %d - Blocks = %d", count_pages, count_empty_blocks((page_t*) list_entry(tmp, page_header_t, node)));
					count_pages++;
				}

				LOG_DEBUG("\t\t\tMLFQ %d has %d pages", mlfq, count_pages);
			}
		}
	}


#ifdef LOG_LEVEL_STATS
	LOG_STATS("########--LOCAL POOL STATS--########");
	long int total_mallocs = 0;
	long int time_malloc_total = 0;
	long int time_malloc_system_call = 0;
	long int time_malloc_shared_pool = 0;
	for (thread = 0; thread < pool->count_threads; ++thread) {
		LOG_STATS("\tThread %d", thread);
		thread_data = (pool->thread_data + thread);
		LOG_STATS("\t\tCount Malloc Ops = %ld", thread_data->count_malloc);
		LOG_STATS("\t\tCount MLFQ changes = %ld (%f %)", thread_data->count_mlfq_change, (float)(thread_data->count_mlfq_change * 100) / thread_data->count_malloc);
		LOG_STATS("\t\tCount Block counting ops = %ld", thread_data->count_blocks_counting_ops);
		LOG_STATS("\t\tCount Shared pool requests = %ld (%f %)", thread_data->count_request_shared_pool, (float)(thread_data->count_request_shared_pool * 100) / thread_data->count_malloc);
		LOG_STATS("\t\tCount Back 2 Shared pool = %ld", thread_data->count_back2_shared_pool);
		LOG_STATS("\t\tTotal time spent on malloc = %ld", thread_data->time_malloc_total);
		LOG_STATS("\t\tTotal time spent on malloc system call = %ld", thread_data->time_malloc_system_call);
		LOG_STATS("\t\tTotal time spent in shared pool = %ld (%f %)", thread_data->time_malloc_shared_pool, (float)(thread_data->time_malloc_shared_pool * 100) / thread_data->time_malloc_total);
		total_mallocs += thread_data->count_malloc;
		time_malloc_total += thread_data->time_malloc_total;
		time_malloc_system_call += thread_data->time_malloc_system_call;
		time_malloc_shared_pool += thread_data->time_malloc_shared_pool;
	}
	LOG_STATS("\t Malloc Ops = %ld", total_mallocs);
	LOG_STATS("\t Total time malloc = %ld", time_malloc_total);
	LOG_STATS("\t Total time malloc system call = %ld (%f %)", time_malloc_system_call, (float)(time_malloc_system_call * 100) / time_malloc_total);
	LOG_STATS("\t Malloc time in shared pool = %ld (%f %)", time_malloc_shared_pool, (float)(time_malloc_shared_pool * 100) / time_malloc_total);
#endif

	LOG_EPILOG();
}
