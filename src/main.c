#include <stdio.h>
#include <stdlib.h>
#include "includes/utils.h"
#include "includes/logger.h"
#include "includes/page.h"
#include "includes/local_pool.h"
#include "includes/queue.h"
#include <pthread.h>

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

    local_pool_t* pool = create_local_pool();

    void* block = NULL;
	int i = 0;
	int count4 = 0, count8 = 0, count256 = 0, count512=0;
	for (i = 0; i < MAX_BLOCKS_IN_PAGE; ++i) {
		block = malloc_block_from_pool(pool, 1);
		if (block != NULL) {
			count4++;
		}

		block = malloc_block_from_pool(pool, 5);
		if (block != NULL) {
			count8++;
		}
		block = malloc_block_from_pool(pool, 253);
		if (block != NULL) {
			count256++;
		}

		block = malloc_block_from_pool(pool, 260);
		if (block != NULL) {
			count512++;
		}
	}

    LOG_INFO("Num blocks allocated: 4 bytes = %d, 8 bytes = %d, 256 bytes = %d, 512 bytes = %d", count4, count8, count256, count512);
    LOG_EPILOG();
}

typedef struct test_data_wf_queue {
	int thread_id;
	int count_enque_ops;
	wf_queue_head_t *q;
	wf_queue_op_head_t *op_desc;
} test_data_wf_queue_t;

void* test_func_wf_queue(void* thread_data) {
    LOG_PROLOG();

	test_data_wf_queue_t* data = (test_data_wf_queue_t*)thread_data;

	int i = 0;
	for (i = 0; i < data->count_enque_ops; ++i) {
		wf_enqueue(data->q, create_wf_queue_node(), data->op_desc, data->thread_id);
	}

	LOG_EPILOG();
	return NULL;
}

void test_wf_queue() {
    LOG_PROLOG();

    const int COUNT_THREADS = 50;
    const int COUNT_ENQUEUE_OPS = 100;

    wf_queue_head_t *q = create_wf_queue();
    wf_queue_op_head_t *op_desc = create_queue_op_desc(COUNT_THREADS);
    pthread_t threads[COUNT_THREADS];
    test_data_wf_queue_t thread_data[COUNT_THREADS];

    int i = 0;
    for (i = 0; i < COUNT_THREADS; ++i) {
    	thread_data[i].thread_id = i;
    	thread_data[i].count_enque_ops = COUNT_ENQUEUE_OPS;
    	thread_data[i].q = q;
    	thread_data[i].op_desc = op_desc;

    	LOG_INFO("Creating thread %d", i);
    	pthread_create(threads + i, NULL, test_func_wf_queue, thread_data+i);
    }

    for (i = 0; i < COUNT_THREADS; ++i) {
    	pthread_join(threads[i], NULL);
    }

    wf_queue_node_t *x = q->head;
    i=0;
    while(x!=NULL){
    	LOG_INFO("Stamp %d = %d", i++, x->stamp);
    	x=x->next;
    }

	LOG_EPILOG();
}

int main() {
    LOG_INIT_CONSOLE();
    LOG_INIT_FILE();

    //test_page();
    //test_local_pool();
    test_wf_queue();

    LOG_CLOSE();
    return 0;
}
