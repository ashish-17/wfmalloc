/*
 * queue.c
 *
 *  Created on: Jan 2, 2016
 *      Author: ashish
 */

#include <stdlib.h>
#include <stdbool.h>
#include "includes/queue.h"
#include "includes/logger.h"
#include "includes/atomic.h"
#include "includes/utils.h"
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>

//#define CAS_TAG(old_stamped_ref, expected_stamped_ref, new_stamped_ref) (compare_and_swap_ptr (&(old_stamped_ref), (expected_stamped_ref), (new_stamped_ref)))

void help(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase);
long max_phase(wf_queue_op_head_t* op_desc);
bool is_pending(wf_queue_op_head_t* op_desc, long phase, int thread_id);
void help_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase);
void help_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase);
void help_finish_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id);
void help_finish_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id);

#ifdef DEBUG_QUEUE
typedef struct debug_data {
	int index;
	wf_queue_node_t *tail;
	int stepEnquueThreadId;
	wf_queue_node_t* old_stamped_ref_next;
	wf_queue_node_t* new_stamped_ref_next;

	int stepOpDescUpdateThreadId;
	wf_queue_op_desc_t* old_stamped_ref_op_desc;
	wf_queue_op_desc_t* new_stamped_ref_op_desc;

	int stepTailUpdateStepThreadId;
	wf_queue_node_t* old_stamped_ref_tail;
	wf_queue_node_t* new_stamped_ref_tail;
} debug_data_t;

#define COUNT_TEST_THREADS 10
#define COUNT_OPS_PER_THREAD 500000
int* debug_queue_op_index;
debug_data_t** debug_queue_data;
#endif

#ifdef DEBUG
#define COUNT_TEST_THREADS 10
#define COUNT_OPS_PER_THREAD 500000
#endif

wf_queue_head_t* create_wf_queue(wf_queue_node_t* sentinel) {
	LOG_PROLOG();

	wf_queue_head_t* queue = (wf_queue_head_t*)malloc(sizeof(wf_queue_head_t));
	queue->head = GET_TAGGED_PTR(sentinel, wf_queue_node_t, 0);
	queue->tail = queue->head;

#ifdef DEBUG_QUEUE
	debug_queue_op_index = (int*) malloc(sizeof(int) * COUNT_TEST_THREADS);
	debug_queue_data = (debug_data_t**) malloc(sizeof(debug_data_t*) * COUNT_TEST_THREADS);
	
	int i = 0, j = 0;
	
	for (i = 0; i < COUNT_TEST_THREADS; i++) {
	    debug_queue_data[i] = (debug_data_t*) malloc(sizeof(debug_data_t) * COUNT_OPS_PER_THREAD);
	}	
	
	for (i = 0; i < COUNT_TEST_THREADS; ++i) {
		for (j = 0; j < COUNT_OPS_PER_THREAD; ++j) {
			debug_queue_data[i][j].index = j;

			debug_queue_data[i][j].tail = NULL;

			debug_queue_data[i][j].stepEnquueThreadId = -1;
			debug_queue_data[i][j].old_stamped_ref_next = NULL;
			debug_queue_data[i][j].new_stamped_ref_next = NULL;

			debug_queue_data[i][j].stepOpDescUpdateThreadId = -1;
			debug_queue_data[i][j].old_stamped_ref_op_desc = NULL;
			debug_queue_data[i][j].new_stamped_ref_op_desc = NULL;

			debug_queue_data[i][j].stepTailUpdateStepThreadId = -1;
			debug_queue_data[i][j].old_stamped_ref_tail = NULL;
			debug_queue_data[i][j].new_stamped_ref_tail = NULL;
		}

		debug_queue_op_index[i] = -1;
	}
#endif
#ifdef DEBUG
	int i;
	nodes_dequeued = (int*)malloc(sizeof(int) * (COUNT_OPS_PER_THREAD + 1));
        for (i = 0; i < (COUNT_OPS_PER_THREAD + 1); i++) {
	    nodes_dequeued[i] = -1;
	}
#endif
	LOG_EPILOG();
	return queue;
}

void init_wf_queue_node(wf_queue_node_t* node) {
	LOG_PROLOG();

	node->next = NULL;
	node->enq_tid = -1;
	node->deq_tid = -1;

	LOG_EPILOG();
}

wf_queue_node_t* create_wf_queue_node() {
	LOG_PROLOG();

	wf_queue_node_t* node = (wf_queue_node_t*)malloc(sizeof(wf_queue_node_t));
	init_wf_queue_node(node);

	LOG_EPILOG();
	return node;
}

wf_queue_op_head_t* create_queue_op_desc(int num_threads) {
	LOG_PROLOG();

	wf_queue_op_head_t* op_desc = (wf_queue_op_head_t*)malloc(sizeof(wf_queue_op_head_t));
	op_desc->num_threads = num_threads;
	op_desc->ops = malloc(num_threads * sizeof(wf_queue_op_desc_t*));
	op_desc->ops_reserve = malloc(num_threads * sizeof(wf_queue_op_desc_t*));

	wf_queue_op_desc_t* ops = (wf_queue_op_desc_t*)malloc(2 * num_threads * sizeof(wf_queue_op_desc_t));
	wf_queue_op_desc_t* ops_reserve = (ops + num_threads);
	int thread = 0;
	for (thread = 0; thread < num_threads; ++thread) {
		*(op_desc->ops + thread) = GET_TAGGED_PTR(ops + thread, wf_queue_op_desc_t, 0);
		*(op_desc->ops_reserve + thread) = GET_TAGGED_PTR(ops_reserve + thread, wf_queue_op_desc_t, 0);

		(ops + thread)->phase = -1;
		(ops + thread)->enqueue = 0;
		(ops + thread)->pending = 0;
		(ops + thread)->node = NULL;
		(ops + thread)->queue = NULL;

		(ops_reserve + thread)->phase = -1;
		(ops_reserve + thread)->enqueue = 0;
		(ops_reserve + thread)->pending = 0;
		(ops_reserve + thread)->node = NULL;
		(ops_reserve + thread)->queue = NULL;
	}

	LOG_EPILOG();
	return op_desc;
}

void wf_enqueue(wf_queue_head_t *q, wf_queue_node_t* node, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	long phase = max_phase(op_desc) + 1;

#ifdef DEBUG_QUEUE
	debug_queue_op_index[thread_id]++;
#endif

	wf_queue_op_desc_t* old_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread_id), wf_queue_op_desc_t);
	wf_queue_op_desc_t* new_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops_reserve + thread_id), wf_queue_op_desc_t);

	new_op_desc_ref->phase = phase;

	node->enq_tid = thread_id;
	node->next = NULL;

	new_op_desc_ref->node = node;
	new_op_desc_ref->pending = 1;
	new_op_desc_ref->enqueue = 1;
	new_op_desc_ref->queue = q;

	int new_stamp = GET_TAG_FROM_TAGGEDPTR(*(op_desc->ops + thread_id)) + 1;

	*(op_desc->ops + thread_id) = GET_TAGGED_PTR(new_op_desc_ref, wf_queue_op_desc_t, new_stamp);
	*(op_desc->ops_reserve + thread_id) = old_op_desc_ref;

	help(q, op_desc, thread_id, phase);
	help_finish_enq(q, op_desc, thread_id);

	LOG_EPILOG();
}

wf_queue_node_t* wf_dequeue(wf_queue_head_t *q, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	wf_queue_node_t* node = NULL;
	long phase = max_phase(op_desc) + 1;

	wf_queue_op_desc_t* old_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread_id), wf_queue_op_desc_t);
	//assert(old_op_desc_ref->pending == 0);
	wf_queue_op_desc_t* new_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops_reserve + thread_id), wf_queue_op_desc_t);

	new_op_desc_ref->phase = phase;
	new_op_desc_ref->node = NULL;
	new_op_desc_ref->pending = true;
	new_op_desc_ref->enqueue = false;
	new_op_desc_ref->queue = q;

	unsigned int new_stamp = GET_TAG_FROM_TAGGEDPTR(*(op_desc->ops + thread_id)) + 1;
	//LOG_INFO("stamp for thread %d is %d", thread_id, new_stamp);

	*(op_desc->ops + thread_id) = GET_TAGGED_PTR(new_op_desc_ref, wf_queue_op_desc_t, new_stamp);
	*(op_desc->ops_reserve + thread_id) = old_op_desc_ref;

	help(q, op_desc, thread_id, phase);
	help_finish_deq(q, op_desc, thread_id);

	node = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread_id), wf_queue_op_desc_t)->node;
	/*if (unlikely(node == NULL)) {
		LOG_WARN("Dequeued node is NULL");
	}*/
        #ifdef DEBUG
	if (node) {
	    //LOG_INFO("thread %d dequeued %d", thread_id, node->index);
	    //assert(GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread_id), wf_queue_op_desc_t)->pending== 0);
	    if (node->deq_tid != thread_id) {
    	        LOG_DEBUG("node->deq_tid = %d, thread_id = %d, node_index = %d, pending = %d", node->deq_tid , thread_id, node->index, GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread_id), wf_queue_op_desc_t)->pending);	    
	    }
	}
	#endif
	LOG_EPILOG();
	return node;
}

void help(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase) {
	LOG_PROLOG();

	int thread = 0;
	int num_threads = op_desc->num_threads;
	wf_queue_op_desc_t* op_desc_ref = NULL;
	for (thread = 0; thread < num_threads; ++thread) {
		op_desc_ref = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread), wf_queue_op_desc_t);
		if ((op_desc_ref->pending == 1) && (op_desc_ref->phase <= phase)) {
			if (unlikely(op_desc_ref->enqueue == 1)) {
				help_enq(op_desc_ref->queue, op_desc, thread_id, thread, phase);
			} else {
				help_deq(op_desc_ref->queue, op_desc, thread_id, thread, phase);
			}
		}
	}

	LOG_EPILOG();
}

long max_phase(wf_queue_op_head_t* op_desc) {
	LOG_PROLOG();

	long max_phase = -1, phase = -1;
	int thread = 0;
	int num_threads = op_desc->num_threads;
	for (thread = 0; thread < num_threads; ++thread) {
		phase = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread), wf_queue_op_desc_t)->phase;
		if (phase > max_phase) {
			max_phase = phase;
		}
	}

	LOG_EPILOG();
	return max_phase;
}

bool is_pending(wf_queue_op_head_t* op_desc, long phase, int thread_id) {
	LOG_PROLOG();

	wf_queue_op_desc_t* op_desc_ref = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread_id), wf_queue_op_desc_t);
	LOG_EPILOG();
	return ((op_desc_ref->pending == 1) && (op_desc_ref->phase <= phase));
}

// TODO: note: stamp of node->next is not really used. It is always one. 
// Can be a source of future bugs.
void help_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase) {
	LOG_PROLOG();

	while (is_pending(op_desc, phase, thread_to_help)) {
		wf_queue_node_t *last_stamped_ref = queue->tail;
		wf_queue_node_t *next_stamped_ref = GET_PTR_FROM_TAGGEDPTR(last_stamped_ref, wf_queue_node_t)->next;
		wf_queue_node_t *new_node = GET_PTR_FROM_TAGGEDPTR(*(op_desc->ops + thread_to_help), wf_queue_op_desc_t)->node;

		uint32_t old_stamp = GET_TAG_FROM_TAGGEDPTR(next_stamped_ref);
		uint32_t new_stamp = (old_stamp + 1);
		if (last_stamped_ref == queue->tail) {
			if (next_stamped_ref == NULL) {
				if (is_pending(op_desc, phase, thread_to_help)) {
					wf_queue_node_t* new_stamped_ref = GET_TAGGED_PTR(new_node, wf_queue_node_t, new_stamp);
					if (atomic_compare_exchange_strong(&(GET_PTR_FROM_TAGGEDPTR(last_stamped_ref, wf_queue_node_t)->next), &next_stamped_ref, new_stamped_ref)) {
#ifdef DEBUG_QUEUE
						if (debug_queue_data[thread_to_help][debug_queue_op_index[thread_to_help]].stepEnquueThreadId != -1) {
							//assert(debug_queue_data[thread_to_help][debug_queue_op_index[thread_to_help]].stepEnquueThreadId == -1);
						}

						debug_queue_data[thread_to_help][debug_queue_op_index[thread_to_help]].stepEnquueThreadId = thread_id;
						debug_queue_data[thread_to_help][debug_queue_op_index[thread_to_help]].old_stamped_ref_next = next_stamped_ref;
						debug_queue_data[thread_to_help][debug_queue_op_index[thread_to_help]].new_stamped_ref_next = new_stamped_ref;
#endif
						help_finish_enq(queue, op_desc, thread_id);
						return;
					}
				}
			} else {
				help_finish_enq(queue, op_desc, thread_id);
			}
		}
	}
	LOG_EPILOG();
}

void help_finish_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	wf_queue_node_t* tail_stamped_ref = queue->tail;
	wf_queue_node_t* tail_ref = GET_PTR_FROM_TAGGEDPTR(tail_stamped_ref, wf_queue_node_t);

	wf_queue_node_t *next_stamped_ref = tail_ref->next;
	uint32_t old_stamp_tail = GET_TAG_FROM_TAGGEDPTR(tail_stamped_ref);
	uint32_t new_stamp_tail = old_stamp_tail + 1;

	if (GET_PTR_FROM_TAGGEDPTR(next_stamped_ref, wf_queue_node_t) != NULL) {
		int enq_tid = GET_PTR_FROM_TAGGEDPTR(next_stamped_ref, wf_queue_node_t)->enq_tid;

		wf_queue_op_desc_t* old_op_desc_stamped_ref = *(op_desc->ops + enq_tid);
		wf_queue_op_desc_t* old_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(old_op_desc_stamped_ref, wf_queue_op_desc_t);
		uint32_t old_stamp = GET_TAG_FROM_TAGGEDPTR(old_op_desc_stamped_ref);
		uint32_t new_stamp =  old_stamp + 1;

		if ((tail_stamped_ref == queue->tail) &&
			(GET_PTR_FROM_TAGGEDPTR(old_op_desc_ref->node, wf_queue_node_t) == GET_PTR_FROM_TAGGEDPTR(next_stamped_ref, wf_queue_node_t))) {

			wf_queue_op_desc_t* new_op_desc_stamped_ref = GET_TAGGED_PTR(*(op_desc->ops_reserve + thread_id), wf_queue_op_desc_t, new_stamp);
			wf_queue_op_desc_t* new_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(new_op_desc_stamped_ref, wf_queue_op_desc_t);

			new_op_desc_ref->phase = old_op_desc_ref->phase;
			new_op_desc_ref->pending = false;
			new_op_desc_ref->enqueue = true;
			// TODO: difference from paper: paper sets node to next
			new_op_desc_ref->node = NULL;
			new_op_desc_ref->queue = queue;

			if (atomic_compare_exchange_strong((op_desc->ops + enq_tid), &old_op_desc_stamped_ref, new_op_desc_stamped_ref)) {
				*(op_desc->ops_reserve + thread_id) = old_op_desc_ref;
				assert(old_op_desc_ref->pending == 1);
				assert(old_op_desc_ref->node != NULL);
#ifdef DEBUG_QUEUE
				if (debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].stepOpDescUpdateThreadId != -1) {
					//assert(debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].stepOpDescUpdateThreadId == -1);
				}

				debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].stepOpDescUpdateThreadId = thread_id;
				debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].old_stamped_ref_op_desc = old_op_desc_stamped_ref;
				debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].new_stamped_ref_op_desc = new_op_desc_stamped_ref;
#endif
			}

			wf_queue_node_t* new_stamped_ref = GET_TAGGED_PTR(GET_PTR_FROM_TAGGEDPTR(next_stamped_ref, wf_queue_node_t), wf_queue_node_t, new_stamp_tail);
			if (atomic_compare_exchange_strong(&(queue->tail), &tail_stamped_ref, new_stamped_ref)) {
#ifdef DEBUG_QUEUE

				if (debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].stepTailUpdateStepThreadId != -1) {
				//	assert(debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].stepTailUpdateStepThreadId == -1);
				}

				debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].stepTailUpdateStepThreadId = thread_id;
				debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].old_stamped_ref_tail = tail_stamped_ref;
				debug_queue_data[enq_tid][debug_queue_op_index[enq_tid]].new_stamped_ref_tail = new_stamped_ref;
#endif
			}
		} else {
			/*LOG_DEBUG("tail_stamped_ref == %p, queue->tail = %p", tail_stamped_ref, queue->tail);
			LOG_DEBUG("old_op_desc_ref->node = %p, next_stamped_ref = %p", old_op_desc_ref->node, next_stamped_ref);
			LOG_DEBUG("enq_tid = %d, thread_id = %d", enq_tid, thread_id);*/
		}
	} else {
		//LOG_DEBUG("==NULL");
	}

	LOG_EPILOG();
}

void help_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase) {
	LOG_PROLOG();

	while (is_pending(op_desc, phase, thread_to_help)) {

		wf_queue_node_t *first_stamped_ref = queue->head;
		wf_queue_node_t *last_stamped_ref = queue->tail;
		wf_queue_node_t *next_stamped_ref = GET_PTR_FROM_TAGGEDPTR(first_stamped_ref, wf_queue_node_t)->next;
		wf_queue_node_t *first = GET_PTR_FROM_TAGGEDPTR(first_stamped_ref, wf_queue_node_t);
		wf_queue_node_t *last = GET_PTR_FROM_TAGGEDPTR(last_stamped_ref, wf_queue_node_t);
		wf_queue_node_t *next = GET_PTR_FROM_TAGGEDPTR(next_stamped_ref, wf_queue_node_t);

		if (first_stamped_ref == queue->head) {
			if (first_stamped_ref == last_stamped_ref) { // queue might be empty
				if (next == NULL) { // queue is empty
					//LOG_INFO("found queue empty. thread_id = %d, thread_to_help = %d", thread_id, thread_to_help);
					wf_queue_op_desc_t* old_op_desc_stamped_ref = *(op_desc->ops + thread_to_help);
					wf_queue_op_desc_t* old_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(old_op_desc_stamped_ref, wf_queue_op_desc_t);
					uint32_t old_stamp = GET_TAG_FROM_TAGGEDPTR(old_op_desc_stamped_ref);
					uint32_t new_stamp =  old_stamp + 1;

					if ((last_stamped_ref == queue->tail) && is_pending(op_desc, phase, thread_to_help)) {

					        wf_queue_op_desc_t* new_op_desc_stamped_ref = GET_TAGGED_PTR(*(op_desc->ops_reserve + thread_id), wf_queue_op_desc_t, new_stamp);
					        wf_queue_op_desc_t* new_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(new_op_desc_stamped_ref, wf_queue_op_desc_t);

					        new_op_desc_ref->phase = old_op_desc_ref->phase;
					        new_op_desc_ref->pending = false;
					        new_op_desc_ref->enqueue = false;
					        new_op_desc_ref->node = NULL;
					        new_op_desc_ref->queue = queue;
						//if (old_op_desc_ref->pending && atomic_compare_exchange_strong((op_desc->ops + thread_to_help), &old_op_desc_stamped_ref, new_op_desc_stamped_ref)) {
						if (atomic_compare_exchange_strong((op_desc->ops + thread_to_help), &old_op_desc_stamped_ref, new_op_desc_stamped_ref)) {
							// TODO: should we append stamped_ref or just ref is ok?
							*(op_desc->ops_reserve + thread_id) = old_op_desc_ref;
						        //assert(old_op_desc_ref->pending == 1);
						}
					}	
				} else {
					help_finish_enq(queue, op_desc, thread_id);
				}
			} else {   // queue is not empty

				wf_queue_op_desc_t* old_op_desc_stamped_ref = *(op_desc->ops + thread_to_help);
				wf_queue_op_desc_t* old_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(old_op_desc_stamped_ref, wf_queue_op_desc_t);
                                uint32_t old_stamp = GET_TAG_FROM_TAGGEDPTR(old_op_desc_stamped_ref);
				uint32_t new_stamp =  old_stamp + 1;
				//wf_queue_node_t* node = old_op_desc_ref->node;

				if (!is_pending(op_desc, phase, thread_to_help)) {
					break;
				}

				if ((first_stamped_ref == queue->head) && (GET_PTR_FROM_TAGGEDPTR(old_op_desc_ref->node, wf_queue_node_t) != first)) {
					#ifdef DEBUG
					if (old_op_desc_ref->node) {
					    //assert(first->index >= old_op_desc_ref->node->index);
					}	
					#endif
				        wf_queue_op_desc_t* new_op_desc_stamped_ref = GET_TAGGED_PTR(*(op_desc->ops_reserve + thread_id), wf_queue_op_desc_t, new_stamp);
				        wf_queue_op_desc_t* new_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(new_op_desc_stamped_ref, wf_queue_op_desc_t);
					new_op_desc_ref->phase = old_op_desc_ref->phase;
					new_op_desc_ref->pending = true;
					new_op_desc_ref->enqueue = false;
					// TODO: is this correct?; stamp is 0
					// Q: why is node ptr of a op_desc a staped rerf. not incrementing its stamp anywhere?
					new_op_desc_ref->node = first;
					new_op_desc_ref->queue = queue;
					//if (old_op_desc_ref->pending && atomic_compare_exchange_strong((op_desc->ops + thread_to_help), &old_op_desc_stamped_ref, new_op_desc_stamped_ref)) {
					if (atomic_compare_exchange_strong((op_desc->ops + thread_to_help), &old_op_desc_stamped_ref, new_op_desc_stamped_ref)) {
						*(op_desc->ops_reserve + thread_id) = old_op_desc_ref;
						if (old_op_desc_ref->pending == 0) {
						    assert(0);
						}
					} else {
					    continue;
					}
				}
				int minus_one_value = -1;
				if (atomic_compare_exchange_strong(&(first->deq_tid), &minus_one_value, thread_to_help)) {
				    nodes_dequeued[first->index] = thread_to_help;
				}
				help_finish_deq(queue, op_desc, thread_id);
			}
		}
	}
	LOG_EPILOG();
}

void help_finish_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	wf_queue_node_t *first_stamped_ref = queue->head;
	wf_queue_node_t *next_stamped_ref = GET_PTR_FROM_TAGGEDPTR(first_stamped_ref, wf_queue_node_t)->next;
	wf_queue_node_t *first = GET_PTR_FROM_TAGGEDPTR(first_stamped_ref, wf_queue_node_t);
	wf_queue_node_t *next = GET_PTR_FROM_TAGGEDPTR(next_stamped_ref, wf_queue_node_t);

	int old_stamp_head = GET_TAG_FROM_TAGGEDPTR(first_stamped_ref);
	int new_stamp_head = old_stamp_head + 1;
	
	int deq_id = first->deq_tid;

	if (deq_id != -1) {
		wf_queue_op_desc_t* old_op_desc_stamped_ref = *(op_desc->ops + deq_id);
		wf_queue_op_desc_t* old_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(old_op_desc_stamped_ref, wf_queue_op_desc_t);
		uint32_t old_stamp = GET_TAG_FROM_TAGGEDPTR(old_op_desc_stamped_ref);
		uint32_t new_stamp =  old_stamp + 1;

		if ((first_stamped_ref == queue->head) && (next != NULL)) {
			if (1) {
		//	if ((old_op_desc_ref->pending)) {
			wf_queue_op_desc_t* new_op_desc_stamped_ref = GET_TAGGED_PTR(*(op_desc->ops_reserve + thread_id), wf_queue_op_desc_t, new_stamp);
		        wf_queue_op_desc_t* new_op_desc_ref = GET_PTR_FROM_TAGGEDPTR(new_op_desc_stamped_ref, wf_queue_op_desc_t);
			new_op_desc_ref->phase = old_op_desc_ref->phase;
			new_op_desc_ref->pending = false;
			new_op_desc_ref->enqueue = false;
			// TODO: is this correct?; stamp is 0
			// Q: why is node ptr of a op_desc a staped rerf. not incrementing its stamp anywhere?
			new_op_desc_ref->node = old_op_desc_ref->node;
			new_op_desc_ref->queue = queue;

			//if ( old_op_desc_ref->pending && atomic_compare_exchange_strong((op_desc->ops + deq_id), &old_op_desc_stamped_ref, new_op_desc_stamped_ref)) {
			if (old_op_desc_ref->pending && atomic_compare_exchange_strong((op_desc->ops + deq_id), &old_op_desc_stamped_ref, new_op_desc_stamped_ref)) {
				*(op_desc->ops_reserve + thread_id) = old_op_desc_ref;
				assert(old_op_desc_ref->pending == 1);
				assert(old_op_desc_ref->node != NULL);	
			}
			}
			wf_queue_node_t* new_stamped_ref = GET_TAGGED_PTR(GET_PTR_FROM_TAGGEDPTR(next_stamped_ref, wf_queue_node_t), wf_queue_node_t, new_stamp_head);
			if (atomic_compare_exchange_strong(&(queue->head), &first_stamped_ref, new_stamped_ref)) {
			}	
		}
	}

	LOG_EPILOG();
}

int wf_queue_count_nodes(wf_queue_head_t* head) {
	LOG_PROLOG();

	int count = 0;
	wf_queue_node_t* tmp_head = GET_PTR_FROM_TAGGEDPTR(head->head, wf_queue_node_t);
	while (GET_PTR_FROM_TAGGEDPTR(tmp_head->next, wf_queue_node_t) != NULL) {
		count++;
		tmp_head = GET_PTR_FROM_TAGGEDPTR(tmp_head->next, wf_queue_node_t);
	}

	LOG_EPILOG();

	return count;
}
