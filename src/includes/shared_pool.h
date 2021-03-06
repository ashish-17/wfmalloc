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

#ifdef DEBUG
     #include <pthread.h>
#endif
struct local_pool;

typedef struct shared_thread_data {
	wf_queue_head_t* bins[MAX_BINS];
} shared_thread_data_t;

typedef struct shared_pool {
	int count_processors;
	int count_threads;
	shared_thread_data_t* thread_data;
	wf_queue_op_head_t* op_desc;
} shared_pool_t;

shared_pool_t* create_shared_pool(int count_threads);

void add_page_shared_pool(shared_pool_t *pool, page_t *page, int thread_id, int queue_idx);

page_t* get_page_shared_pool(shared_pool_t *pool, struct local_pool* l_pool, int thread_id, int queue_idx, int block_size);

void shared_pool_stats(shared_pool_t* pool);

#ifdef DEBUG
pthread_mutex_t *enq_lock;
pthread_mutex_t *deq_lock;
int* n_deq_per_q;
int* n_enq_per_q;
int n_processors;
shared_pool_t* s_pool_copy;
#endif

#endif /* INCLUDES_SHARED_POOL_H_ */
