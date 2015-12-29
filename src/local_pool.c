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

local_pool_t* create_local_pool() {
	LOG_PROLOG();

	local_pool_t *pool = (local_pool_t*) malloc(sizeof(local_pool_t));
	int bin = 0;
	page_t* ptr_page = NULL;
	int page_idx = 0;
	int block_size = MIN_BLOCK_SIZE;
	for (bin = 0; bin < MAX_BINS; ++bin) {
		INIT_LIST_HEAD(pool->bins + bin);
		for (page_idx = 0; page_idx < MIN_PAGES_PER_BIN; ++page_idx) {
			ptr_page = create_page(block_size);
			list_add(&(ptr_page->header.node), pool->bins + bin);
		}

		block_size *= 2;
	}

	LOG_EPILOG();
	return pool;
}

void add_page(local_pool_t *pool, page_t *page) {
	LOG_PROLOG();

	int bin_idx = quick_log2(page->header.block_size) - quick_log2(MIN_BLOCK_SIZE);
	list_add_tail(&(page->header.node), pool->bins + bin_idx);

	LOG_EPILOG();
}

page_t* remove_page(local_pool_t *pool) {
	return NULL;
}

void* malloc_block_from_pool(local_pool_t *pool, int block_size) {
	LOG_PROLOG();

	void* block = NULL;
	int bin_idx = 0;
	if (block_size > MIN_BLOCK_SIZE) {
		bin_idx = quick_log2(upper_power_of_two(block_size)) - quick_log2(MIN_BLOCK_SIZE);
	}

	list_t* tmp;
	list_for_each(tmp, (pool->bins+bin_idx)) {
		block = malloc_block((page_t*)list_entry(tmp, page_header_t, node));
		if (block != NULL) {
			break;
		}
	}

	LOG_EPILOG();
	return block;
}
