#ifndef INCLUDES_LOCAL_POOL_H_
#define INCLUDES_LOCAL_POOL_H_

#include "list.h"
#include "config.h"
#include "page.h"
#include "shared_pool.h"
#include "logger.h"
#include <stdint.h>

typedef struct local_thread_data {
	list_t bins[MAX_BINS][MAX_MLFQ]; // Every bin has a multi level feedback queue structure.
	list_t* tail[MAX_BINS][MAX_MLFQ]; // Each MLFQ has to maintain tail -> beginning
        page_t* page_cache[MAX_BINS]; // Use pages from these until empty
	//int last_shared_pool_idx; // Each threads keeps track of last used shared pool/queue and then fetches next in round robin fashion.
	int mlfq_to_assess; //  a page at tail needs to be assessed and inserted in the right mlfq
	int total_pages[MAX_BINS]; // total number of pages in a bin

#ifdef LOG_LEVEL_STATS
        long int count_request_shared_pool;
        long int count_back2_shared_pool;
        //long int count_mlfq_change;
        long int count_malloc;
        //long int count_blocks_counting_ops;
        long int time_malloc_total;
        long int time_malloc_shared_pool;
        long int time_malloc_system_call;
#endif

} local_thread_data_t;

typedef struct local_pool {
        int count_threads;
        int count_processors;
	int init_pages_per_bin; // initial pages on creation
        int min_pages_per_bin; // threashold to move pages back to shared pool
        local_thread_data_t* thread_data;
} local_pool_t;

local_pool_t* create_local_pool(int count_threads);

//int add_page(local_pool_t *pool, page_t *page, int thread_id);

void* malloc_block_from_pool(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, int block_size);

void local_pool_stats(local_pool_t *pool);

#endif /* INCLUDES_LOCAL_POOL_H_ */
