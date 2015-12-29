/*
 * local_pool.h
 *
 *  Created on: Dec 28, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_LOCAL_POOL_H_
#define INCLUDES_LOCAL_POOL_H_

#include "list.h"
#include "config.h"
#include "page.h"

typedef struct local_pool {
	list_t bins[MAX_BINS];
} local_pool_t;

local_pool_t* create_local_pool();

void add_page(local_pool_t *pool, page_t *page);

page_t* remove_page(local_pool_t *pool);

void* malloc_block_from_pool(local_pool_t *pool, int block_size);

#endif /* INCLUDES_LOCAL_POOL_H_ */
