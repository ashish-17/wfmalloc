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
#include <stdlib.h>

static local_pool_t* l_pool = NULL;
static shared_pool_t* s_pool = NULL;


void wfinit(int max_count_threads) {
	l_pool = create_local_pool(max_count_threads);
	s_pool = create_shared_pool(max_count_threads);
}

void* wfmalloc(size_t bytes, int thread_id) {
	void* mem = NULL;
	if (bytes < PAGE_SIZE) {
		mem = get_mem(l_pool, s_pool, thread_id, bytes);
	} else {
		mem = get_big_mem(s_pool, thread_id, bytes);
	}

	return mem;
}

void wffree(void* ptr, int thread_id) {

	mem_block_header_t* mem_block = (mem_block_header_t*)((char*)ptr - sizeof(mem_block_header_t));
	if (mem_block->size < PAGE_SIZE) {
		add_mem(l_pool, s_pool, mem_block, thread_id);
	} else {
		free_big_mem(s_pool, mem_block, thread_id);
	}
}

void wfstats() {
    //LOG_INIT_CONSOLE();
    LOG_INIT_FILE();

	local_pool_sanity_check(l_pool);
	//shared_pool_stats(s_pool);

    LOG_CLOSE();
}

