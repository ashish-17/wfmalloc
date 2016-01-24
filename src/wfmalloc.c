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
#include <stdlib.h>

static local_pool_t* l_pool = NULL;
static shared_pool_t* s_pool = NULL;


void wfinit(int max_count_threads) {
	l_pool = create_local_pool(max_count_threads);
	s_pool = create_shared_pool(max_count_threads);
}

void* wfmalloc(size_t bytes, int thread_id) {
	return malloc_block_from_pool(l_pool, s_pool, thread_id, bytes);;
}

void wffree(void* ptr) {
	free_block(ptr);
}

void wfstats() {
	local_pool_stats(l_pool);
	shared_pool_stats(s_pool);
}

