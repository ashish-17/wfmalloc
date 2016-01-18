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

typedef struct wfmalloc_data {
	local_pool_t* l_pool;
	shared_pool_t* s_pool;
} wfmalloc_data_t;

static wfmalloc_data_t *wf_data = NULL;

void wfinit(int max_count_threads) {
	wf_data = (wfmalloc_data_t*)malloc(sizeof(wfmalloc_data_t));
	wf_data->l_pool = create_local_pool(max_count_threads);
	wf_data->s_pool = create_shared_pool(max_count_threads);
}

void* wfmalloc(size_t bytes, int thread_id) {
	return malloc_block_from_pool(wf_data->l_pool, wf_data->s_pool, thread_id, bytes);;
}

void wffree(void* ptr) {
	free_block(ptr);
}


