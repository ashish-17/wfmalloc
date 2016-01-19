#include <stdio.h>
#include <stdlib.h>
#include "includes/utils.h"
#include "includes/logger.h"
#include "includes/page.h"
#include "includes/local_pool.h"
#include "includes/queue.h"
#include "includes/shared_pool.h"
#include "wfmalloc.h"
#include <pthread.h>
#include <string.h>

void test_page() {
    LOG_PROLOG();

    page_header_t header;
    LOG_INFO("Size of header = %d", sizeof(header));
    LOG_INFO("Size of bitmap = %d", sizeof(header.block_flags));

    LOG_INFO("Address header = %u", &header);
    LOG_INFO("Offset block size = %u", OFFSETOF(page_header_t, block_size));
    LOG_INFO("Offset max blocks = %u", OFFSETOF(page_header_t, max_blocks));
    LOG_INFO("Offset bitmap = %u", OFFSETOF(page_header_t, block_flags));

    page_t *ptr = create_page(4);
    LOG_INFO("test page block size = %d", ptr->header.block_size);
    LOG_INFO("test page num blocks = %d", ptr->header.max_blocks);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));

    void* blk1 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    void* blk2 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    void* blk3 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    void* blk4 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));

    free_block(blk3);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    free_block(blk4);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    free_block(blk2);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    free_block(blk1);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));

    LOG_EPILOG();
}

void test_local_pool() {
    LOG_PROLOG();

    local_pool_t* pool = create_local_pool(2);
    local_pool_stats(pool);

    LOG_EPILOG();
}

typedef struct dummy_data_pools {
	int thread_id;
	local_pool_t* l_pool;
	shared_pool_t* s_pool;
	int count_malloc_ops;
} dummy_data_pools_t;

void* test_pools(void* data) {
	dummy_data_pools_t* thread_data = (dummy_data_pools_t*)data;
	int i = 0;
	void** blocks = malloc(sizeof(void*) * thread_data->count_malloc_ops);
	for (i = 0; i < thread_data->count_malloc_ops; ++i) {
		blocks[i] = malloc_block_from_pool(thread_data->l_pool, thread_data->s_pool, thread_data->thread_id, 4);
	}

	for (i = 0; i < thread_data->count_malloc_ops; ++i) {
		free_block(blocks[i]);
	}

	return NULL;
}

void test_pools_multi_thread() {
    const int COUNT_THREADS = 4;
    const int MALLOC_OPS = 309*2;

	local_pool_t* l_pool = create_local_pool(COUNT_THREADS);
	shared_pool_t* s_pool = create_shared_pool(COUNT_THREADS);

	dummy_data_pools_t dummy_data[COUNT_THREADS];
	pthread_t threads[COUNT_THREADS];
	int i = 0;
	for (i = 0; i < COUNT_THREADS; ++i) {
		dummy_data[i].thread_id = i;
		dummy_data[i].l_pool = l_pool;
		dummy_data[i].s_pool = s_pool;
		dummy_data[i].count_malloc_ops = MALLOC_OPS;

		pthread_create(threads + i, NULL, test_pools, dummy_data + i);
	}

	for (i = 0; i < COUNT_THREADS; ++i) {
		pthread_join(threads[i], NULL);
	}
}

void test_pools_single_thread() {
    LOG_PROLOG();
    const int COUNT_THREADS = 1;

	local_pool_t* l_pool = create_local_pool(COUNT_THREADS);
	shared_pool_t* s_pool = create_shared_pool(COUNT_THREADS);

	void* block = NULL;
	int i = 0;
	int count4 = 0, count8 = 0, count256 = 0, count512 = 0;
	for (i = 0; i < 1545; ++i) {
		block = malloc_block_from_pool(l_pool, s_pool, 0, 4);
		if (block != NULL) {
			count4++;
		}

		block = malloc_block_from_pool(l_pool, s_pool, 0, 16);
		if (block != NULL) {
			count8++;
		}
		block = malloc_block_from_pool(l_pool, s_pool, 0, 256);
		if (block != NULL) {
			count256++;
		}

		block = malloc_block_from_pool(l_pool, s_pool, 0, 512);
		if (block != NULL) {
			count512++;
		}
	}

	local_pool_stats(l_pool);
	LOG_INFO("Num blocks allocated: 4 bytes = %d, 8 bytes = %d, 256 bytes = %d, 512 bytes = %d", count4, count8, count256, count512);

	LOG_EPILOG();
}

typedef struct dummy_data_wf_queue {
	int data;
	wf_queue_node_t node;
} dummy_data_wf_queue_t;

typedef struct test_data_wf_queue {
	int thread_id;
	int count_enque_ops;
	int count_deque_ops;
	wf_queue_head_t *q;
	wf_queue_op_head_t *op_desc;
	dummy_data_wf_queue_t* dummy_data;
} test_data_wf_queue_t;

void* test_func_wf_queue(void* thread_data) {
    LOG_PROLOG();

	test_data_wf_queue_t* data = (test_data_wf_queue_t*)thread_data;

	int i = 0;
	for (i = 0; i < data->count_enque_ops; ++i) {
		wf_enqueue(data->q, &(data->dummy_data[i].node), data->op_desc, data->thread_id);
	}

	for (i = 0; i < data->count_deque_ops; ++i) {
		wf_dequeue(data->q, data->op_desc, data->thread_id);
	}

	LOG_EPILOG();
	return NULL;
}

void test_wf_queue() {
    LOG_PROLOG();

    const int COUNT_THREADS = 400;
    const int COUNT_ENQUEUE_OPS = 100;
    const int COUNT_DeQUEUE_OPS = 50;

    // Result = 1 + COUNT_THREADS*COUNT_ENQUEUE_OPS - COUNT_THREADS*COUNT_DeQUEUE_OPS

    dummy_data_wf_queue_t dummy_data[COUNT_THREADS*COUNT_ENQUEUE_OPS + 1];
    int i = 0;
    for (i = 0; i < (COUNT_THREADS*COUNT_ENQUEUE_OPS + 1); ++i) {
    	dummy_data[i].data = i;
    	init_wf_queue_node(&(dummy_data[i].node));
    }

    wf_queue_head_t *q = create_wf_queue(&(dummy_data[0].node));
    wf_queue_op_head_t *op_desc = create_queue_op_desc(COUNT_THREADS);
    pthread_t threads[COUNT_THREADS];
    test_data_wf_queue_t thread_data[COUNT_THREADS];

    for (i = 0; i < COUNT_THREADS; ++i) {
    	thread_data[i].thread_id = i;
    	thread_data[i].count_enque_ops = COUNT_ENQUEUE_OPS;
    	thread_data[i].count_deque_ops = COUNT_DeQUEUE_OPS;
    	thread_data[i].q = q;
    	thread_data[i].op_desc = op_desc;
    	thread_data[i].dummy_data = dummy_data + i*COUNT_ENQUEUE_OPS+1;

    	LOG_INFO("Creating thread %d", i);
    	pthread_create(threads + i, NULL, test_func_wf_queue, thread_data+i);
    }

    for (i = 0; i < COUNT_THREADS; ++i) {
    	pthread_join(threads[i], NULL);
    }

    int verify[COUNT_THREADS*COUNT_ENQUEUE_OPS];
    memset(verify, 0, sizeof(verify));

    wf_queue_node_t *x = q->head->ref;
    int total=0;
    while(x!=NULL){
    	dummy_data_wf_queue_t* val = (dummy_data_wf_queue_t*)list_entry(x, dummy_data_wf_queue_t, node);
    	if (verify[val->data] == 1) {
    		LOG_WARN("Duplicate = %d", val->data);
    	} else {
    		verify[val->data] = 1;
    	}

    	total++;
    	x=x->next->ref;
    }

    int count_miss = 0;
    int count_found = 0;
    for (i = 0; i < COUNT_THREADS*COUNT_ENQUEUE_OPS+1; ++i) {
		if (verify[i] == 1) {
			count_found++;
		} else {
			count_miss++;
		}
    }

    LOG_INFO("Number of missed items = %d", count_miss);
    LOG_INFO("Number of items enqueued = %d", count_found);
    LOG_INFO("Total number of items in queue = %d", total);

	LOG_EPILOG();
}

void test_wf_dequeue() {
    LOG_PROLOG();

    wf_queue_head_t *q = create_wf_queue(create_wf_queue_node());
    wf_queue_op_head_t *op_desc = create_queue_op_desc(1);
    int i = 0;
	for (i = 0; i < 50; ++i) {
    	wf_enqueue(q, create_wf_queue_node(), op_desc, 0);
    	wf_enqueue(q, create_wf_queue_node(), op_desc, 0);
		//wf_dequeue(q, op_desc, 0);
	}

	wf_queue_node_t *x = q->head->ref;
	i=0;
	int total = 0;
	while (x != NULL) {
		LOG_INFO("Queue item %d", i++);
		x = x->next->ref;
		total++;
	}

    LOG_INFO("Total number of items in queue = %d", total);

	LOG_EPILOG();
}

void* test_worker_wfmalloc(void* data) {
    LOG_PROLOG();

    const int COUNT_MALLOC_OPS = 100;
    int thread_id = *((int*)data);
    int i = 0;
    void** mem = malloc(sizeof(void*) * COUNT_MALLOC_OPS);
    for (i = 0; i < COUNT_MALLOC_OPS; ++i) {
    	mem[i] = wfmalloc(4, thread_id);
    	wffree(mem[i]);
    }

	LOG_EPILOG();
	return NULL;
}

void test_wfmalloc() {
    LOG_PROLOG();

    const int COUNT_THREADS = 10;

    wfinit(COUNT_THREADS);

    pthread_t threads[COUNT_THREADS];
    int data[COUNT_THREADS];

    int i = 0;
    for (i = 0; i < COUNT_THREADS; ++i) {
    	data[i] = i;
    	pthread_create(threads + i, NULL, test_worker_wfmalloc, data + i);
    }

    for (i = 0; i < COUNT_THREADS; ++i) {
    	pthread_join(threads[i], NULL);
    }

	LOG_EPILOG();
}

int main() {
    LOG_INIT_CONSOLE();
    LOG_INIT_FILE();

    //test_page();
    //test_wf_queue();
    //test_wf_dequeue();
    //test_local_pool();
    //test_pools_single_thread();
    //test_pools_multi_thread();
    test_wfmalloc();

    LOG_CLOSE();
    return 0;
}
