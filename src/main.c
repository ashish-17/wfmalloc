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
#include <assert.h>
#include <unistd.h>

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

pthread_barrier_t barr;

void* test_func_wf_queue(void* thread_data) {
    LOG_PROLOG();

	test_data_wf_queue_t* data = (test_data_wf_queue_t*)thread_data;

	pthread_barrier_wait(&barr);
	int i = 0;
	for (i = 0; i < data->count_enque_ops; ++i) {
		wf_enqueue(data->q, &(data->dummy_data[i].node), data->op_desc, data->thread_id);
	}

	LOG_DEBUG("Finished Enqueue - %d", data->thread_id);
	/*LOG_DEBUG("Start Dequeue - %d", data->thread_id);

	for (i = 0; i < data->count_deque_ops; ++i) {
		wf_dequeue(data->q, data->op_desc, data->thread_id);
	}

	LOG_DEBUG("Finished Dequeue - %d", data->thread_id);*/

	LOG_EPILOG();
	return NULL;
}

void test_wf_queue() {
    LOG_PROLOG();

    const int COUNT_THREADS = 10;
    const int COUNT_ENQUEUE_OPS = 20000;
    const int COUNT_DeQUEUE_OPS = 0;

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
    pthread_barrier_init(&barr, NULL, COUNT_THREADS);

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
    int* sizes = malloc(sizeof(int) * COUNT_MALLOC_OPS);
    int i = 0;
    void** mem = malloc(sizeof(void*) * COUNT_MALLOC_OPS);
    for (i = 0; i < COUNT_MALLOC_OPS; ++i) {
    	sizes[i] = (rand() % 100)+1;
    	mem[i] = wfmalloc(sizes[i], thread_id);

    	/*mem_block_header_t *block_header = (mem_block_header_t*)((char*)mem[i] - sizeof(mem_block_header_t));
    	page_t* page_ptr = (page_t*)((char*)block_header - block_header->byte_offset);

    	LOG_INFO("Requested - %d, Returned = %d", sizes[i], page_ptr->header.block_size);*/
    }

	for (i = 0; i < COUNT_MALLOC_OPS; ++i) {
		wffree(mem[i]);
	}

	LOG_EPILOG();
	return NULL;
}

void write_to_bytes(char* string, int n_bytes) {
    LOG_PROLOG();
    char start = 'a';
    int i = 0;
    for (i = 0; i < n_bytes; i++) {
        string[i] = start;
	start++;
    }
    LOG_EPILOG();
}

void test_bytes(char* string, int n_bytes) {
	LOG_PROLOG();
	char start = 'a';
    	int i = 0;
	for (i = 0; i < n_bytes; i++) {
		assert(string[i] == start);
		start++;
	}
	LOG_EPILOG();
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

void* test_worker_wfmalloc_bug(void* data) {
    LOG_PROLOG();

    const int COUNT_MALLOC_OPS = 500000;
    int thread_id = *((int*)data);
    int* sizes = malloc(sizeof(int) * COUNT_MALLOC_OPS);
    int i = 0;
    char* mem[COUNT_MALLOC_OPS];
    int n_bytes[COUNT_MALLOC_OPS];
    for (i = 0; i < COUNT_MALLOC_OPS; ++i) {
    	sizes[i] = (rand() % 100)+1;
    	mem[i] = wfmalloc(sizes[i], thread_id);

    	mem_block_header_t *block_header = (mem_block_header_t*)((char*)(mem[i]) - sizeof(mem_block_header_t));
    	page_t* page_ptr = (page_t*)((char*)block_header - block_header->byte_offset);

		unsigned page_size = page_ptr->header.block_size;

		if (sizes[i] > page_size) {
			LOG_INFO("Requested - %d, Returned = %d", sizes[i], page_size);
			assert(sizes[i] <= page_size);
		}

		write_to_bytes(mem[i], n_bytes[i]);
	}

	for (i = 0; i < COUNT_MALLOC_OPS; ++i) {
		test_bytes(mem[i], n_bytes[i]);
		wffree(mem[i]);
	}

	LOG_EPILOG();
	return NULL;
}

void test_wfmalloc_bug(int COUNT_THREADS) {
    LOG_PROLOG();

    wfinit(COUNT_THREADS);

    pthread_t threads[COUNT_THREADS];
    int data[COUNT_THREADS];

    int i = 0;
    for (i = 0; i < COUNT_THREADS; ++i) {
    	data[i] = i;
    	pthread_create(threads + i, NULL, test_worker_wfmalloc_bug, data + i);
    }

    for (i = 0; i < COUNT_THREADS; ++i) {
    	pthread_join(threads[i], NULL);
    }

	LOG_EPILOG();
}

typedef struct _ThreadData {
    int allocatorNo;
    int nThreads;
    int minSize;
    int maxSize;
    int blocksPerThread;
    int threadId;
    char **blkp;
    long count;
} ThreadData;

int startClock;
int stopClock;

int randomNumber(int min, int max) {
	return (min + (rand() % (max - min + 1)));
}

void distribute(int blocksPerThread, int minSize, int maxSize, char** blkp, int allocatorNo, int distributorId) {
    LOG_PROLOG();

	int blkSize, i = 0;
	for (i = 0; i < blocksPerThread; i++) {
		blkSize = randomNumber(minSize, maxSize);
		if (allocatorNo == 1) {
			blkp[i] = (char*) wfmalloc(blkSize, distributorId);
		} else {
			blkp[i] = (char*) malloc(blkSize);
		}
	}

	LOG_EPILOG();
}

void initialRoundOfAllocationFree(int numOfBlocks, int minSize, int maxSize, int allocatorNo, int threadId) {
    LOG_PROLOG();

	int blkSize, victim, i = 0;
	char *blkp[numOfBlocks];

	for (i = 0; i < numOfBlocks; i++) {
		blkSize = randomNumber(minSize, maxSize);
		if (allocatorNo == 1) {
			blkp[i] = (char*) wfmalloc(blkSize, threadId);
		} else {
			blkp[i] = (char*) malloc(blkSize);
		}
	}

	for (i = 0; i < numOfBlocks; i++) {
		victim = randomNumber(0, numOfBlocks - i - 1);
		if (allocatorNo == 1) {
			wffree(blkp[victim]);
		} else {
			free(blkp[victim]);
		}
		blkp[victim] = blkp[numOfBlocks - i - 1];
		blkp[numOfBlocks - i - 1] = NULL;
	}

	LOG_EPILOG();
}

void* worker(void *data) {
    LOG_PROLOG();

	while (startClock != 1);

	ThreadData* threadData = (ThreadData*) data;
	int victim, blkSize;

	while (1) {
		victim = randomNumber(0, threadData->blocksPerThread - 1);
		if (threadData->allocatorNo == 1) {
			wffree(threadData->blkp[victim]);
		} else {
			free(threadData->blkp[victim]);
		}
		blkSize = randomNumber(threadData->minSize, threadData->maxSize);
		if (threadData->allocatorNo == 1) {
			threadData->blkp[victim] = (char*) wfmalloc(blkSize,
					threadData->threadId);
		} else {
			threadData->blkp[victim] = (char*) malloc(blkSize);
		}
		threadData->count++;
		if (stopClock == 1) {
			break;
		}
	}

	LOG_EPILOG();
}

void test_larson(int allocatorNo, int nThreads,  int numOfBlocks, int minSize, int maxSize, unsigned timeSlice) {
    LOG_PROLOG();

	startClock = 0;
	stopClock = 0;

	int numBlocksPerThread;
	long int totalCount = 0;

	srand(time(NULL));

	ThreadData threadData[nThreads];
	pthread_t threads[nThreads];

	if (allocatorNo == 1) {
		wfinit(nThreads + 1);
	}

	// rounding down numOfBlocks to nearest multiple of nThreads
	numBlocksPerThread = numOfBlocks / nThreads;
	numOfBlocks = numBlocksPerThread * nThreads;

	char *blkp[numOfBlocks];
	initialRoundOfAllocationFree(numOfBlocks, minSize, maxSize, allocatorNo, nThreads);
	int t = 0, i=0;
	for (i = 0; i < nThreads; i++) {
		distribute(numBlocksPerThread, minSize, maxSize, (blkp + (i * numBlocksPerThread)), allocatorNo, nThreads);
	}

	for (t = 0; t < nThreads; t++) {
		threadData[t].minSize = minSize;
		threadData[t].maxSize = maxSize;
		threadData[t].blocksPerThread = numBlocksPerThread;
		threadData[t].threadId = t;
		threadData[t].allocatorNo = allocatorNo;
		threadData[t].nThreads = nThreads;
		threadData[t].blkp = (blkp + (t * numBlocksPerThread));
		threadData[t].count = 0;
		pthread_create((threads + t), NULL, worker, (threadData + t));
	}

	startClock = 1;
	sleep(timeSlice);
	stopClock = 1;

	for (t = 0; t < nThreads; t++) {
		pthread_join(threads[t], NULL);
	}

	for (t = 0; t < nThreads; t++) {
		totalCount += threadData[t].count;
	}

	printf("\n%ld", totalCount);

	LOG_EPILOG();
}

int main() {
    LOG_INIT_CONSOLE();
    LOG_INIT_FILE();

    //test_page();
    test_wf_queue();
    //test_wf_dequeue();
    //test_local_pool();
    //test_pools_single_thread();
    //test_pools_multi_thread();
    //test_wfmalloc();
    //test_larson(1, 5, 10000, 4, 8, 30);
    //test_larson(0, 4, 1000, 4, 8, 5);

    //test_wfmalloc_bug(10);

	//wfstats();

    LOG_CLOSE();
    return 0;
}
