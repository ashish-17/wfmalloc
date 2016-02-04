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
	if (bytes > MAX_BLOCK_SIZE) {
		return lalloc(bytes);
	} else {
		void* v = malloc_block_from_pool(l_pool, s_pool, thread_id, bytes);;
		unsigned page_size = ((page_t*)(((uintptr_t)v) & PAGE_MASK))->header.block_size;
		if(page_size < bytes) printf("Failure: %p %u %u\n", v, bytes, page_size);
		assert(page_size >= bytes);
		return v;
	}
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

