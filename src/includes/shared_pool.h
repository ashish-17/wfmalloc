/*
 * shared_pool.h
 *
 *  Created on: Jan 12, 2016
 *      Author: ashish
 */

#ifndef INCLUDES_SHARED_POOL_H_
#define INCLUDES_SHARED_POOL_H_

#include "queue.h"
#include "config.h"
#include "page.h"
#include "page_heap.h"

struct local_pool;

typedef struct shared_thread_data {
	wf_queue_head_t* bins[MAX_BINS];
	wf_queue_op_head_t* op_desc[MAX_BINS];
	int count_ops[MAX_BINS];
	int count_pages_alloc[MAX_BINS];
} shared_thread_data_t;

typedef struct shared_pool {
	int count_processors;
	int count_threads;
	shared_thread_data_t* thread_data;
	page_heap_t* ph;
} shared_pool_t;

shared_pool_t* create_shared_pool(int count_threads);

void add_mem_shared_pool(shared_pool_t *pool, mem_block_header_t *mem, int thread_id, int queue_idx);

mem_block_header_t* get_mem_shared_pool(shared_pool_t *pool, int thread_id, int queue_idx, int block_size);

void shared_pool_stats(shared_pool_t* pool);

#endif /* INCLUDES_SHARED_POOL_H_ */
