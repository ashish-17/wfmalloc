/*
 * wfmalloc.c
 *
 *  Created on: Jan 17, 2016
 *      Author: ashish
 */
#include "wfmalloc.h"
#include "includes/local_pool.h"
#include "includes/shared_pool.h"
#include "includes/page.h"
#include "includes/logger.h"
#include "includes/large_allocations.h"
#include <stdlib.h>
#include <assert.h>
static local_pool_t* l_pool = NULL;
static shared_pool_t* s_pool = NULL;

void wfinit(int max_count_threads) {
	l_pool = create_local_pool(max_count_threads);
	s_pool = create_shared_pool(max_count_threads);
}

void* wfmalloc(size_t bytes, int thread_id) {
	void *v;
	unsigned page_size;
	if (bytes > MAX_BLOCK_SIZE) {
		v = lalloc(bytes);
		#ifdef DEBUG
		page_size = get_num_pages_from_payload(v) * PAGE_SIZE; 
		if(page_size < bytes) LOG_ERROR("Failure: %p %u %u\n", v, bytes, page_size);
		LOG_INFO("Asked for %u got %u at %p.", bytes, page_size, v);
		#endif
	} else {
		v = malloc_block_from_pool(l_pool, s_pool, thread_id, bytes);;
		#ifdef DEBUG
		page_size = ((page_t*)(((uintptr_t)v) & PAGE_MASK))->header.block_size;
		if(page_size < bytes) LOG_ERROR("Failure: %p %u %u\n", v, bytes, page_size);
		//LOG_INFO("Asked for %u got %u at %p.", bytes, page_size, v);
		#endif
	}
	return v;
}

void wffree(void* ptr) {
	if (is_large_allocation(ptr)) {
	    lfree(ptr);
	} else {
	    free_block(ptr);
	}
}

void wfstats() {
    //LOG_INIT_CONSOLE();
    LOG_INIT_FILE();

    local_pool_stats(l_pool);
    //shared_pool_stats(s_pool);

    LOG_CLOSE();
}

