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
	int number_ops;
	wf_queue_head_t *q;
	wf_queue_op_head_t *op_desc;
	dummy_data_wf_queue_t* dummy_data;
	wf_queue_node_t** queue_data;
} test_data_wf_queue_t;

pthread_barrier_t barr,barr1;

void* test_func_wf_queue(void* thread_data) {
	LOG_PROLOG();

	test_data_wf_queue_t* data = (test_data_wf_queue_t*)thread_data;

	pthread_barrier_wait(&barr);
	int i = 0;
	for (i = 0; i < data->count_enque_ops; ++i) {
		wf_enqueue(data->q, &(data->dummy_data[i].node), data->op_desc, data->thread_id);
	}

	LOG_DEBUG("Finished Enqueue - %d", data->thread_id);

	pthread_barrier_wait(&barr1);
	LOG_DEBUG("Start Dequeue - %d", data->thread_id);

	wf_queue_node_t *x = NULL;
	dummy_data_wf_queue_t* val = NULL;
	int perThreadSanityFails = 0;
	for (i = 0; i < data->count_deque_ops; ++i) {
		x = wf_dequeue(data->q, data->op_desc, data->thread_id);
		val = (dummy_data_wf_queue_t*) list_entry(x, dummy_data_wf_queue_t, node);

		if (val->data != x->sanityData) {
			perThreadSanityFails++;
		}
	}

	LOG_DEBUG("Finished Dequeue - %d", data->thread_id);
	if (perThreadSanityFails > 0) {
		LOG_DEBUG("Sanity Fails in dequeue - %d", perThreadSanityFails);
	}

	LOG_EPILOG();
	return NULL;
}

void test_wf_queue() {
	LOG_PROLOG();

	const int COUNT_THREADS = 10;
	const int COUNT_ENQUEUE_OPS = 900000;
	const int COUNT_DEQUEUE_OPS = 900000;

	// Result = 1 + COUNT_THREADS*COUNT_ENQUEUE_OPS - COUNT_THREADS*COUNT_DeQUEUE_OPS

	dummy_data_wf_queue_t *dummy_data = (dummy_data_wf_queue_t*) malloc(sizeof(dummy_data_wf_queue_t) * (COUNT_THREADS*COUNT_ENQUEUE_OPS + 1));

	int i = 0;
	for (i = 0; i < (COUNT_THREADS*COUNT_ENQUEUE_OPS + 1); ++i) {
		dummy_data[i].data = i;
		init_wf_queue_node(&(dummy_data[i].node));
		dummy_data[i].node.sanityData = i;
	}

	wf_queue_head_t *q = create_wf_queue(&(dummy_data[0].node));
	wf_queue_op_head_t *op_desc = create_queue_op_desc(COUNT_THREADS);
	pthread_t threads[COUNT_THREADS];
	pthread_barrier_init(&barr, NULL, COUNT_THREADS);
	pthread_barrier_init(&barr1, NULL, COUNT_THREADS);

	test_data_wf_queue_t thread_data[COUNT_THREADS];

	for (i = 0; i < COUNT_THREADS; ++i) {
		thread_data[i].thread_id = i;
		thread_data[i].count_enque_ops = COUNT_ENQUEUE_OPS;
		thread_data[i].count_deque_ops = COUNT_DEQUEUE_OPS;
		thread_data[i].q = q;
		thread_data[i].op_desc = op_desc;
		thread_data[i].dummy_data = dummy_data + i*COUNT_ENQUEUE_OPS+1;

		LOG_INFO("Creating thread %d", i);
		pthread_create(threads + i, NULL, test_func_wf_queue, thread_data+i);
	}

	for (i = 0; i < COUNT_THREADS; ++i) {
		pthread_join(threads[i], NULL);
	}

	int *verify = (int*)malloc(sizeof(int) * (COUNT_THREADS*(COUNT_ENQUEUE_OPS) + 1));
	memset(verify, 0, sizeof(int) * (COUNT_THREADS*(COUNT_ENQUEUE_OPS) + 1));

	wf_queue_node_t *x = GET_PTR_FROM_TAGGEDPTR(q->head, wf_queue_node_t);
	int total=0;
	int sanityFails = 0;
	while(x!=NULL){
		dummy_data_wf_queue_t* val = (dummy_data_wf_queue_t*)list_entry(x, dummy_data_wf_queue_t, node);
		if (val->data > (COUNT_THREADS*COUNT_ENQUEUE_OPS)) {
			LOG_WARN("Invalid value in node - %d", val->data);
		}
		else {
			if (verify[val->data] == 1) {
				LOG_WARN("Duplicate = %d", val->data);
			} else {
				verify[val->data] = 1;
			}
		}

		if (val->data != val->node.sanityData) {
			sanityFails++;
		}
		total++;
		x=GET_PTR_FROM_TAGGEDPTR(x->next, wf_queue_node_t);
	}

	int count_miss = 0;
	int count_found = 0;
	for (i = 0; i < COUNT_THREADS*(COUNT_ENQUEUE_OPS)+1; ++i) {
		if (verify[i] == 1) {
			count_found++;
		} else {
			count_miss++;
		}
	}

	LOG_INFO("Number of missed items = %d", count_miss);
	LOG_INFO("Number of sanity fails = %d", sanityFails);
	LOG_INFO("Number of items enqueued = %d", count_found);
	LOG_INFO("Total number of items in queue = %d", total);

	LOG_EPILOG();
}

void* test_func_wf_dequeue(void* thread_data) {
	LOG_PROLOG();

	test_data_wf_queue_t* data = (test_data_wf_queue_t*)thread_data;
	pthread_barrier_wait(&barr);

	int i = 0;
	while(1) {
		data->queue_data[i] = wf_dequeue(data->q, data->op_desc, data->thread_id);
		if (data->queue_data[i] == NULL) {
			return NULL;
		}
		i++;
	}
	return NULL;
}


int check_queue(wf_queue_head_t *q, int total_ops) {
	wf_queue_node_t* head = GET_PTR_FROM_TAGGEDPTR(q->head, wf_queue_node_t);
	wf_queue_node_t* tail = GET_PTR_FROM_TAGGEDPTR(q->tail, wf_queue_node_t);
	int i = 0;
	int error = 0;
	while (head != tail) {
		dummy_data_wf_queue_t* val = (dummy_data_wf_queue_t*)list_entry(head, dummy_data_wf_queue_t, node);
		if (head->sanityData != i) {
			LOG_WARN("head->index = %d, i = %d", head->sanityData, i);
			error = 1;
		}
		if (val->data != i) {
			LOG_ERROR("val->data = %d, i = %d", val->data, i);
			error = 1;
		}
		i++;
		head = GET_PTR_FROM_TAGGEDPTR(head->next, wf_queue_node_t);
	}
	if (i != total_ops) {
		error = 1;
	}
	return error;
}

int test_wf_dequeue(unsigned COUNT_THREADS) {
	LOG_PROLOG();

	const int COUNT_OPS = 50000;
	const int TEST_ITEMS = COUNT_THREADS * COUNT_OPS + 1;

	dummy_data_wf_queue_t *dummy_data = (dummy_data_wf_queue_t*) malloc(
			sizeof(dummy_data_wf_queue_t) * (COUNT_THREADS * COUNT_OPS + 1));

	LOG_INFO("dummy_data = %p", dummy_data);
	int i = 0;
	for (i = 0; i < (COUNT_THREADS * COUNT_OPS + 1); ++i) {
		dummy_data[i].data = i;
		init_wf_queue_node(&(dummy_data[i].node));
#ifdef DEBUG
		dummy_data[i].node.sanityData = i;
#endif
	}

	wf_queue_head_t *q = create_wf_queue(&(dummy_data[0].node));
	wf_queue_op_head_t *op_desc = create_queue_op_desc(COUNT_THREADS);
	pthread_t threads[COUNT_THREADS];

	for (i = 1; i < COUNT_THREADS * COUNT_OPS + 1; i++) {
		wf_enqueue(q, &(dummy_data[i].node), op_desc, 0);
	}

	LOG_INFO("finished enqueue");
	int res = check_queue(q, COUNT_THREADS * COUNT_OPS);
	if (res) {
		LOG_ERROR("queue is not correctly made");
		assert(res == 0);
	}
	LOG_INFO("queue is correctly made by enqueue : %d", res);

	pthread_barrier_init(&barr, NULL, COUNT_THREADS);
	test_data_wf_queue_t thread_data[COUNT_THREADS];
	for (i = 0; i < COUNT_THREADS; i++) {
		thread_data[i].thread_id = i;
		thread_data[i].q = q;
		thread_data[i].op_desc = op_desc;
		thread_data[i].queue_data = (wf_queue_node_t**) malloc(
				sizeof(wf_queue_node_t*) * (COUNT_THREADS * COUNT_OPS + 1));
		LOG_INFO("Creating thread %d", i);
		pthread_create(threads + i, NULL, test_func_wf_dequeue,
				thread_data + i);
	}

	for (i = 0; i < COUNT_THREADS; ++i) {
		pthread_join(threads[i], NULL);
	}

	LOG_INFO("*****Veryfying*****");

	assert(
			(q->head == q->tail) && (GET_PTR_FROM_TAGGEDPTR(q->head, wf_queue_node_t)->next == NULL));

#ifdef DEBUG
	assert (GET_PTR_FROM_TAGGEDPTR(q->head, wf_queue_node_t)->sanityData == (TEST_ITEMS - 1));
#endif

	int final_tail_tag = GET_TAG_FROM_TAGGEDPTR(q->tail);
	int final_head_tag = GET_TAG_FROM_TAGGEDPTR(q->head);
	LOG_INFO("final tail stamp = %d, final head stamp = %d", final_tail_tag,
			final_head_tag);
	assert(final_tail_tag == final_head_tag);

	wf_queue_node_t* x;
	int j = 0;
	int duplicate_cnt = 0;
	int total = 0;
	int* verify = (int*) malloc(sizeof(int) * COUNT_THREADS * COUNT_OPS);
	memset(verify, 0, sizeof(verify));
	int* duplicate_id = (int*) malloc(sizeof(int) * COUNT_THREADS * COUNT_OPS);

	int ullu = 0;
	for (i = 0; i < COUNT_THREADS; i++) {
		j = 0;
		while ((x = thread_data[i].queue_data[j]) != NULL) {
			x = GET_PTR_FROM_TAGGEDPTR(x, wf_queue_node_t);
			dummy_data_wf_queue_t* val = (dummy_data_wf_queue_t*) list_entry(x,
					dummy_data_wf_queue_t, node);
#ifdef DEBUG
			if (x->sanityData != val->data) {
				ullu++;
			}

			//assert(x->index == val->data);
#endif
			verify[val->data]++;

			if (verify[val->data] > 1) {
				LOG_WARN("Duplicate = %d. Thread1 = %d, thread2 = %d",
						val->data, duplicate_id[val->data], i);
			} else {
				duplicate_id[val->data] = i;
			}
			total++;
			j++;
		}
		//	LOG_INFO("thread %d dequeued %d items", i, j);
	}

	int count_miss = 0;
	int count_found = 0;
	for (i = 0; i < COUNT_THREADS * COUNT_OPS; ++i) {
		if (verify[i] == 0) {
			LOG_WARN("Missed Item = %d", i);
			count_miss++;
		} else if (verify[i] == 1) {
			//LOG_INFO("%d dequeued by %d", i, duplicate_id[i]);
			count_found++;
		} else {
			duplicate_cnt++;
			//		LOG_WARN("Found %d duplicates of %d\n", verify[i], i);
		}
	}

	printf("Number of missed items = %d\n", count_miss);
	printf("Number of duplicates = %d\n", duplicate_cnt);
	printf("Total number of items dequeued = %d\n", total);
	printf("Corrupted nodes = %d\n", ullu);
	LOG_EPILOG();
	if ((count_miss != 0) || (duplicate_cnt != 0))
		return 1;
	else return 0;
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

void test_wf_enq_deq() {
	LOG_PROLOG();

	int i;
	int j;
	int res = 0;
	unsigned no_ops[] = {10, 50000, 500000};
	// unsigned no_ops[] = {10};
	for (j = 0; j < sizeof(no_ops)/sizeof(int); j++) {
		for(i = 2; i < 33; i+=5) {
			LOG_INFO("starting the test with %d threads and %u operations",i,no_ops[j]);
			res += simple_test(i, no_ops[j]);
		}
	}

	LOG_INFO("no of times the tests failed = %d", res);

	//random_test();



	LOG_EPILOG();
}
void* simple_test_helper(void* thread_data) {
	LOG_PROLOG();

	test_data_wf_queue_t* data = (test_data_wf_queue_t*)thread_data;
	pthread_barrier_wait(&barr);
	int number_ops = data->number_ops;
	int i;
	for (i = 0; i < number_ops; i++) {
		wf_enqueue(data->q, data->queue_data[i], data->op_desc, data->thread_id);
		//LOG_INFO("thread %d enqueuing %d", data->thread_id, data->queue_data[i]->index);
		data->queue_data[i] = NULL;
	}

#ifdef DEBUG
	for (i = 0; i < number_ops; i++) {
		assert(data->queue_data[i] == NULL);
	}
#endif

	for (i = 0; i < number_ops; i++) {
		data->queue_data[i] = wf_dequeue(data->q, data->op_desc, data->thread_id);
		if (data->queue_data[i] == NULL) {
			LOG_ERROR("thread %d could not dequeue", data->thread_id)
				assert(data->queue_data[i] != NULL);
		} else {
			//LOG_INFO("thread %d dequeued %d", data->thread_id, data->queue_data[i]->index);
		}
	}

	LOG_EPILOG();
	return NULL;
}

int simple_test(unsigned count_threads, unsigned number_of_nodes) {
	LOG_PROLOG();
	//    int number_of_nodes = 1;

	dummy_data_wf_queue_t *dummy_data = (dummy_data_wf_queue_t*) malloc(sizeof(dummy_data_wf_queue_t) * (count_threads * number_of_nodes + 1));

	LOG_INFO("dummy_data = %p", dummy_data);
	int i = 0;
	for (i = 0; i < (count_threads * number_of_nodes + 1); ++i) {
		dummy_data[i].data = i;
		init_wf_queue_node(&(dummy_data[i].node));
#ifdef DEBUG
		dummy_data[i].node.sanityData = i;
#endif
	}

	wf_queue_head_t *q = create_wf_queue(&(dummy_data[0].node));
	wf_queue_op_head_t *op_desc = create_queue_op_desc(count_threads);

	pthread_t threads[count_threads];
	pthread_barrier_init(&barr, NULL, count_threads);

	int j = 0;
	int k = 1;
	test_data_wf_queue_t thread_data[count_threads];
	for ( i = 0; i < count_threads; i++) {
		thread_data[i].thread_id = i;
		thread_data[i].q = q;
		thread_data[i].op_desc = op_desc;
		thread_data[i].queue_data = (wf_queue_node_t**) malloc(sizeof(wf_queue_node_t*) * (number_of_nodes));
		thread_data[i].number_ops = number_of_nodes;

		for (j = 0; j < number_of_nodes; j++) {
			thread_data[i].queue_data[j] = &(dummy_data[k].node);
			k++;
		}

		LOG_INFO("Creating thread %d", i);
		pthread_create(threads + i, NULL, simple_test_helper, thread_data+i);
	}

	for (i = 0; i < count_threads; ++i) {
		pthread_join(threads[i], NULL);
	}

	LOG_INFO("*****Veryfying*****");
	// assert that there is a single node left in the queue -- the one which was enqueued last
	assert ((q->head == q->tail) && (GET_PTR_FROM_TAGGEDPTR(q->head, wf_queue_node_t)->next == NULL));

	int final_tail_tag = GET_TAG_FROM_TAGGEDPTR(q->tail);
	int final_head_tag = GET_TAG_FROM_TAGGEDPTR(q->head);
	LOG_INFO("final tail stamp = %d, final head stamp = %d", final_tail_tag, final_head_tag);
	assert(final_tail_tag == final_head_tag);


	wf_queue_node_t* x;
	int duplicate_cnt = 0;
	int total = 0;
	int* verify = (int*) malloc(sizeof(int)* (count_threads * number_of_nodes + 1));
	memset(verify, 0, sizeof(int)* (count_threads * number_of_nodes + 1));
	int* duplicate_id = (int*) malloc(sizeof(int)* (count_threads * number_of_nodes + 1));

	// there will be one node left in the queue
	// the node which was enqueued last
	// mark in the verify array that it is not a missed node but a node which was left

	x = GET_PTR_FROM_TAGGEDPTR(q->head, wf_queue_node_t);
	dummy_data_wf_queue_t* last_node = (dummy_data_wf_queue_t*)list_entry(x, dummy_data_wf_queue_t, node);
	verify[last_node->data]++;

	int ullu = 0;
	for(i = 0; i < count_threads; i++) {
		for (j = 0; j < number_of_nodes; j++) {
			x = thread_data[i].queue_data[j];
			x = GET_PTR_FROM_TAGGEDPTR(x, wf_queue_node_t);
			dummy_data_wf_queue_t* val = (dummy_data_wf_queue_t*)list_entry(x, dummy_data_wf_queue_t, node);
#ifdef DEBUG
			if (x->sanityData != val->data) {
				ullu++;
			}

			assert(x->sanityData == val->data);
#endif
			verify[val->data]++;

			if (verify[val->data] > 1) {
				LOG_WARN("Duplicate = %d. Thread1 = %d, thread2 = %d", val->data, duplicate_id[val->data], i);
			} else {
				duplicate_id[val->data] = i;
			}
			total++;
		}
		//	LOG_INFO("thread %d dequeued %d items", i, j);
	}

	int count_miss = 0;
	int count_found = 0;
	for (i = 0; i < count_threads * number_of_nodes; ++i) {
		if(verify[i] == 0) {
			LOG_WARN("Missed Item = %d", i);
			count_miss++;
		} else if (verify[i] == 1) {
			//LOG_INFO("%d dequeued by %d", i, duplicate_id[i]);
			count_found++;
		} else {
			duplicate_cnt++;
			//		LOG_WARN("Found %d duplicates of %d\n", verify[i], i);
		}
	}

	printf("\nNumber of missed items = %d\n", count_miss);
	printf("Number of duplicates = %d\n", duplicate_cnt);
	printf("Total number of items dequeued = %d\n", total);
	if ((total != count_threads * number_of_nodes) || (count_miss != 0) || (duplicate_cnt != 0)) {
		return 1;
	} else {
		return 0;
	}

	// Cleaning
	free(verify);
	free(duplicate_id);
	for (i = 0; i < count_threads; i++) {
		free(thread_data[i].queue_data);
	}

	free(dummy_data);

	LOG_EPILOG();
}

wf_queue_node_t* create_linked_list(int num_ops, int thread_id) {
    wf_queue_node_t *linked_list_head;
    wf_queue_node_t *linked_list_tail;

    wf_queue_node_t *node = (wf_queue_node_t*) malloc(sizeof(wf_queue_node_t));
    init_wf_queue_node(node);
    node->sanityData = thread_id;
    linked_list_head = node;
    linked_list_tail = node;

    while(num_ops > 1) {
        node = (wf_queue_node_t*) malloc(sizeof(wf_queue_node_t));
	init_wf_queue_node(node);
	node->sanityData = thread_id;
	linked_list_tail->next = node;
	linked_list_tail = node;	
	num_ops--;
    }

    return linked_list_head;
}

void* test_func_wf_enqueue_multiple(void* thread_data) {
    LOG_PROLOG();
    
    test_data_wf_queue_t* data = (test_data_wf_queue_t*)thread_data;
    	pthread_barrier_wait(&barr);

	int total_num_ops = data->count_enque_ops;
	int num_ops;
	while(total_num_ops != 0) {
	    num_ops = randomNumber(1,5);
	    if (num_ops > total_num_ops) {
	       num_ops = 1;
	    }
	    wf_queue_node_t *node = create_linked_list(num_ops, data->thread_id);
	    wf_enqueue(data->q, node, data->op_desc, data->thread_id);
	    total_num_ops -= num_ops;
	}

    LOG_EPILOG();
    return NULL;
}

int test_wf_enqueue_multiple(unsigned COUNT_THREADS) {
	LOG_PROLOG();

	const int COUNT_OPS = 500000;

	wf_queue_node_t *init_node = (wf_queue_node_t*) malloc(sizeof(wf_queue_node_t));
	init_wf_queue_node(init_node);
	init_node->sanityData = 0;
	wf_queue_head_t *q = create_wf_queue(init_node);
	wf_queue_op_head_t *op_desc = create_queue_op_desc(COUNT_THREADS);
	pthread_t threads[COUNT_THREADS];

	pthread_barrier_init(&barr, NULL, COUNT_THREADS);
	test_data_wf_queue_t thread_data[COUNT_THREADS];
	int i;
	
	for (i = 0; i < COUNT_THREADS; i++) {
		thread_data[i].thread_id = i;
		thread_data[i].q = q;
		thread_data[i].op_desc = op_desc;
		thread_data[i].count_enque_ops = COUNT_OPS;
		LOG_INFO("Creating thread %d", i);
		pthread_create(threads + i, NULL, test_func_wf_enqueue_multiple,
				thread_data + i);
	}

	for (i = 0; i < COUNT_THREADS; ++i) {
		pthread_join(threads[i], NULL);
	}

	LOG_INFO("*****Veryfying*****");
	// Verify that the queue has as COUNT_THREADS * COUNT_OPS + 1 nodes in total
	// such that each thread enqueued COUNT_OPS nodes

	int res = 0;

	wf_queue_node_t* node;
	int nodes_each_thread[COUNT_THREADS];
	for (i = 0; i < COUNT_THREADS; i++) {
	    nodes_each_thread[i] = 0;
	}
	
	node = wf_dequeue(q, op_desc, 0);
	while(node != NULL) {
	    nodes_each_thread[node->sanityData]++;	    
	    free(node);
	    node = wf_dequeue(q, op_desc, 0);
	}

	// accounting for the sentinel node
	nodes_each_thread[0]--;
	// accounting for the left over node in the queue
	node = GET_PTR_FROM_TAGGEDPTR(q->head, wf_queue_node_t);
	nodes_each_thread[node->sanityData]++;
	free(node);

	for (i = 0; i < COUNT_THREADS; i++) {
	    if (nodes_each_thread[i] != COUNT_OPS) {
	        LOG_INFO("thread %d enqueued nodes = %d", i, nodes_each_thread[i]);
		res = 1;
	    }
	}	
	
	LOG_EPILOG();	
	return res;
}

int main() {
	LOG_INIT_CONSOLE();
	LOG_INIT_FILE();

	//test_page();
	//test_wf_queue();
	int res = 0;
	/*
	   for (unsigned i = 10; i < 33; i++) {
	   res += test_wf_dequeue(i);
	   }

	   LOG_INFO("no of times tests failed = %d", res);
	 */
	//test_wf_enq_deq();

	res = 0;
	for (unsigned i = 1; i < 33; i++) {
		LOG_INFO("\n\n Starting test with %d threads", i);
		res += test_wf_enqueue_multiple(i);
	}
	LOG_INFO("no of times tests failed = %d", res);

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
