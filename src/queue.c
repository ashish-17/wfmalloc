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
#include <assert.h>

#define STAMPED_REF_TO_REF(ptr, ref_type) ((ref_type*)((ptr)->ref))
#define CAS_COND1(old_stamped_ref, expected_stamped_ref, expected_ref, expected_stamp, new_stamped_ref) \
					(((old_stamped_ref)->ref == (expected_ref)) && (((old_stamped_ref)->stamp == (expected_stamp))))

#define CAS_COND2(old_stamped_ref, expected_stamped_ref, expected_ref, expected_stamp, new_stamped_ref) \
					(((old_stamped_ref)->ref == (new_stamped_ref)->ref) && (((old_stamped_ref)->stamp == (new_stamped_ref)->stamp)))

#define CAS_COND3(old_stamped_ref, expected_stamped_ref, expected_ref, expected_stamp, new_stamped_ref) \
					(compare_and_swap_ptr (&(old_stamped_ref), (expected_stamped_ref), (new_stamped_ref)))

#define CAS(old_stamped_ref, expected_stamped_ref, expected_ref, expected_stamp, new_stamped_ref) \
	((((old_stamped_ref)->ref == (expected_ref)) && \
			((old_stamped_ref)->stamp == (expected_stamp))) && \
			((((old_stamped_ref)->ref == (new_stamped_ref)->ref) && \
					((old_stamped_ref)->stamp == (new_stamped_ref)->stamp)) || \
					(compare_and_swap_ptr (&(old_stamped_ref), (expected_stamped_ref), (new_stamped_ref)))))

void help(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase);
long max_phase(wf_queue_op_head_t* op_desc);
bool is_pending(wf_queue_op_head_t* op_desc, long phase, int thread_id);
void help_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase);
void help_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase);
void help_finish_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id);
void help_finish_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id);

wf_queue_head_t* create_wf_queue(wf_queue_node_t* sentinel) {
	LOG_PROLOG();

	wf_queue_head_t* queue = (wf_queue_head_t*)malloc(sizeof(wf_queue_head_t));
	queue->head = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));
	queue->head->ref = sentinel;
	queue->head->stamp = 0;
	queue->tail = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));
	queue->tail->ref = sentinel;
	queue->tail->stamp = 0;

	LOG_EPILOG();
	return queue;
}

void init_wf_queue_node(wf_queue_node_t* node) {
	LOG_PROLOG();

	node->next = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));
	node->next->stamp = 0;
	node->next->ref = NULL;
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
		*(op_desc->ops + thread) = (ops + thread);
		*(op_desc->ops_reserve + thread) = (ops_reserve + thread);
		(ops + thread)->phase = -1;
		(ops + thread)->enqueue = 0;
		(ops + thread)->pending = 0;
		(ops + thread)->node = NULL;
		(ops + thread)->queue = NULL;
	}

	op_desc->ref_mem.next_reserve = malloc(num_threads * sizeof(stamped_ref_t*));
	op_desc->ref_mem.head_reserve = malloc(num_threads * sizeof(stamped_ref_t*));
	op_desc->ref_mem.tail_reserve = malloc(num_threads * sizeof(stamped_ref_t*));
	op_desc->ref_mem.ops = malloc(num_threads * sizeof(stamped_ref_t*));
	op_desc->ref_mem.ops_reserve = malloc(num_threads * sizeof(stamped_ref_t*));

	stamped_ref_t* tmp = (stamped_ref_t*)malloc(REF_MEM_COUNT * num_threads * sizeof(stamped_ref_t));
	for (thread = 0; thread < num_threads; ++thread) {
		*(op_desc->ref_mem.next_reserve + thread) = (tmp + thread + num_threads*(REF_MEM_NEXT_RES));
		(*(op_desc->ref_mem.next_reserve + thread))->stamp = 0;
		(*(op_desc->ref_mem.next_reserve + thread))->ref = NULL;

		*(op_desc->ref_mem.head_reserve + thread) = (tmp + thread + num_threads*(REF_MEM_HEAD_RES));
		(*(op_desc->ref_mem.head_reserve + thread))->stamp = 0;
		(*(op_desc->ref_mem.head_reserve + thread))->ref = NULL;

		*(op_desc->ref_mem.tail_reserve + thread) = (tmp + thread + num_threads*(REF_MEM_TAIL_RES));
		(*(op_desc->ref_mem.tail_reserve + thread))->stamp = 0;
		(*(op_desc->ref_mem.tail_reserve + thread))->ref = NULL;

		*(op_desc->ref_mem.ops + thread) = (tmp + thread + num_threads*REF_MEM_OPS);
		(*(op_desc->ref_mem.ops + thread))->stamp = 0;
		(*(op_desc->ref_mem.ops + thread))->ref = *(op_desc->ops + thread);

		*(op_desc->ref_mem.ops_reserve + thread) = (tmp + thread + num_threads*(REF_MEM_OPS_RES));
		(*(op_desc->ref_mem.ops_reserve + thread))->stamp = 0;
		(*(op_desc->ref_mem.ops_reserve + thread))->ref = *(op_desc->ops_reserve + thread);
	}

	LOG_EPILOG();
	return op_desc;
}

#ifdef DEBUG
int (*stop_world)(int);

void cancel_threads(int thread_id, wf_queue_op_head_t* op_desc) {
    
    LOG_PROLOG();

    LOG_DEBUG("Thread %d called check_state", thread_id);
    if (STAMPED_REF_TO_REF(op_desc->ref_mem.ops[thread_id], wf_queue_op_desc_t)->pending == 0) {
        LOG_DEBUG("Weird. Things changed too quickly");
    }

    int n_threads;
    if(stop_world) {
	    n_threads = stop_world(thread_id);
    } else {
	LOG_DEBUG("stop wprld function pointer not set");
        return;
    }

    LOG_DEBUG("World stopped");
    check_state(n_threads, op_desc);
    LOG_EPILOG();
}   

void check_state(int n_threads, wf_queue_op_head_t* op_desc) {

    LOG_PROLOG();
    stamped_ref_t* check_array[n_threads * 5]; // 5 because each thread uses 5 stamped refrence
    wf_thread_reference_memory_t ref_mem = op_desc->ref_mem;
    
//    LOG_DEBUG("Combining the stamped refs");
    int i = 0;
    for (int j = 0; j < n_threads; j++, i++) {
        check_array[i] = ref_mem.next_reserve[j];
	check_array[i + n_threads] = ref_mem.head_reserve[j];
	check_array[i + 2*n_threads] = ref_mem.tail_reserve[j];
	check_array[i + 3*n_threads] = ref_mem.ops[j];
	check_array[i + 4*n_threads] = ref_mem.ops_reserve[j];
    }

//    LOG_DEBUG("checking for duplicates in stamped references");
    for (int j = 0; j < n_threads; j++) {
        for (int  k = j + 1; k < n_threads; k++) {
	    if (check_array[j] == check_array[k]) {
	        LOG_DEBUG("SR %u found equal at indices %d and %d", check_array[j], j ,k);
	    }
	}
    }
/*    
    LOG_DEBUG("Checking pending for sanity again");
    if (STAMPED_REF_TO_REF(op_desc->ref_mem.ops[thread_id], wf_queue_op_desc_t)->pending == 0) {
        LOG_DEBUG("Things are indeed insane in  multiprogramming");
	assert(0); // TODO: bad way to exit
    }
    LOG_DEBUG("World is indeed at rest");
*/
//    LOG_DEBUG("Combining the references now of stamped references");
    i = 0;
    for (int j = 0; j < n_threads; j++, i++) {
        check_array[i] = (ref_mem.next_reserve[j])->ref;
	check_array[i + n_threads] = (ref_mem.head_reserve[j])->ref;
	check_array[i + 2*n_threads] = (ref_mem.tail_reserve[j])->ref;
	check_array[i + 3*n_threads] = (ref_mem.ops[j])->ref;
	check_array[i + 4*n_threads] = (ref_mem.ops_reserve[j])->ref;
    }

//    LOG_DEBUG("checking for duplicates in ref of stamped references");
    for (int j = 0; j < n_threads; j++) {
	if (check_array[j] == NULL) {
	    continue;
	}
        for (int  k = j + 1; k < n_threads; k++) {
	    if ((check_array[k]) && (check_array[j] == check_array[k])) {
	        LOG_DEBUG("SR->ref %u found equal at indices %d and %d", check_array[j], j ,k);
	    }
	}
    }

    LOG_EPILOG();
    return; // Assuming assert will fail next
}

#endif

#ifdef DEBUG
int index[20];
#endif


void wf_enqueue(wf_queue_head_t *q, wf_queue_node_t* node, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	long phase = max_phase(op_desc) + 1;
	stamped_ref_t *old_stamped_ref = *(op_desc->ref_mem.ops + thread_id);
#ifdef DEBUG
	if (STAMPED_REF_TO_REF(old_stamped_ref, wf_queue_op_desc_t)->pending == 1) {
	    cancel_threads(thread_id, op_desc);
	    assert(STAMPED_REF_TO_REF(old_stamped_ref, wf_queue_op_desc_t)->pending == 0);
	}
#endif
	stamped_ref_t *reserve_stamped_ref = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));//*(op_desc->ref_mem.ops_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));

	wf_queue_op_desc_t *op = (wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));//STAMPED_REF_TO_REF(reserve_stamped_ref, wf_queue_op_desc_t);//*(op_desc->ops_reserve + thread_id);//(wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));
	reserve_stamped_ref->ref = op;

	op->phase = phase;

	node->enq_tid = thread_id;
	node->next->ref = NULL;
#ifdef DEBUG
	node->index = index[thread_id];
	index[thread_id]++;
#endif

	op->node = node;
	op->pending = 1;
	op->enqueue = 1;
	op->queue = q;

	reserve_stamped_ref->stamp = old_stamped_ref->stamp + 1;

	*(op_desc->ref_mem.ops_reserve + thread_id) = old_stamped_ref;
	*(op_desc->ref_mem.ops + thread_id) = reserve_stamped_ref;

	help(q, op_desc, thread_id, phase);
	help_finish_enq(q, op_desc, thread_id);

	LOG_EPILOG();
}

wf_queue_node_t* wf_dequeue(wf_queue_head_t *q, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	wf_queue_node_t* node = NULL;

	long phase = max_phase(op_desc) + 1;
	stamped_ref_t *old_stamped_ref = *(op_desc->ref_mem.ops + thread_id);
	assert(STAMPED_REF_TO_REF(old_stamped_ref, wf_queue_op_desc_t)->pending == 0);

	stamped_ref_t *reserve_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);

	wf_queue_op_desc_t *op = STAMPED_REF_TO_REF(reserve_stamped_ref, wf_queue_op_desc_t);//*(op_desc->ops_reserve + thread_id);
	op->phase = phase;
	op->node = NULL;
	op->pending = 1;
	op->enqueue = false;
	op->queue = q;

	reserve_stamped_ref->stamp = old_stamped_ref->stamp + 1;

	*(op_desc->ref_mem.ops_reserve + thread_id) = old_stamped_ref;
	*(op_desc->ref_mem.ops + thread_id) = reserve_stamped_ref;

	help(q, op_desc, thread_id, phase);
	help_finish_deq(q, op_desc, thread_id);

	node = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread_id), wf_queue_op_desc_t)->node;
	/*if (unlikely(node == NULL)) {
		LOG_WARN("Dequeued node is NULL");
	}*/

	LOG_EPILOG();
	return node;
}

void help(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase) {
	LOG_PROLOG();

	int thread = 0;
	int num_threads = op_desc->num_threads;
	wf_queue_op_desc_t* op_tmp = NULL;
	for (thread = 0; thread < num_threads; ++thread) {
		op_tmp = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread), wf_queue_op_desc_t);
		if ((op_tmp->pending == 1) && (op_tmp->phase <= phase)) {
			if (unlikely(op_tmp->enqueue == 1)) {
				help_enq(op_tmp->queue, op_desc, thread_id, thread, phase);
			} else {
				help_deq(op_tmp->queue, op_desc, thread_id, thread, phase);
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
		phase = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread), wf_queue_op_desc_t)->phase;
		if (phase > max_phase) {
			max_phase = phase;
		}
	}

	LOG_EPILOG();
	return max_phase;
}

bool is_pending(wf_queue_op_head_t* op_desc, long phase, int thread_id) {
	LOG_PROLOG();

	wf_queue_op_desc_t* op_tmp = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread_id), wf_queue_op_desc_t);
	LOG_EPILOG();
	return ((op_tmp->pending == 1) && (op_tmp->phase <= phase));
}


#ifdef DEBUG
typedef struct {
    int pending_off;
    int tail_update;
    int pending_off_ctr;
    int tail_update_ctr;
    void* old_tail;
    void* old_ref;
    int old_stamp;
    void* new_tail;
    void *new_ref;
    int new_stamp;
}change_info_t;

uint32_t total_call_help_finish[20];
uint32_t CAS_COND_1[20];
uint32_t CAS_COND_2[20];
uint32_t CAS_COND_3[20];
int found_tail_next_non_null[20];
int can_update_tail[20];
int cond1_true[20];
change_info_t change[10][10000];
#endif

void help_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase) {
	LOG_PROLOG();

#ifdef DEBUG
	total_call_help_finish[thread_id] = 0;
	CAS_COND_1[thread_id] = 0;
	CAS_COND_2[thread_id] = 0;
	CAS_COND_3[thread_id] = 0;
	found_tail_next_non_null[thread_id] = 0;
	can_update_tail[thread_id] = 0;
	cond1_true[thread_id] = 0;
#endif
	while (is_pending(op_desc, phase, thread_to_help)) {
		wf_queue_node_t *last = STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t);
		stamped_ref_t *next = last->next;
		wf_queue_node_t* old_ref_next = STAMPED_REF_TO_REF(next, wf_queue_node_t);
		wf_queue_node_t *new_node = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread_to_help), wf_queue_op_desc_t)->node;

		uint32_t old_stamp = last->next->stamp;
		uint32_t new_stamp = (old_stamp + 1);
		if (last == STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t)) {
			if (STAMPED_REF_TO_REF(last->next, wf_queue_node_t) == NULL) {
				if (is_pending(op_desc, phase, thread_to_help)) {
					stamped_ref_t* node_new_stamped_ref = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));  //*(op_desc->ref_mem.next_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));
					node_new_stamped_ref->ref = new_node;
					node_new_stamped_ref->stamp = new_stamp;
					if (CAS_COND1(STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t)->next, next, old_ref_next, old_stamp, node_new_stamped_ref)) {
						if (CAS_COND2(STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t)->next, next, old_ref_next, old_stamp, node_new_stamped_ref)) {
							help_finish_enq(queue, op_desc, thread_id);
							return;
						} else if (CAS_COND3(STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t)->next, next, old_ref_next, old_stamp, node_new_stamped_ref)) {
							*(op_desc->ref_mem.next_reserve + thread_id) = next; // Recycle stamped reference
							help_finish_enq(queue, op_desc, thread_id);
							return;
						}
					}
				}
			} else {
			#ifdef DEBUG
				total_call_help_finish[thread_id]++;
			#endif
				help_finish_enq(queue, op_desc, thread_id);
			}
		}
	}
	LOG_EPILOG();
}

void help_finish_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	stamped_ref_t* old_stamped_ref_tail = queue->tail;
	wf_queue_node_t *last = STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t);
	stamped_ref_t *next = last->next;
	int old_stamp_tail = queue->tail->stamp;
	int new_stamp_tail = old_stamp_tail + 1;
	if (STAMPED_REF_TO_REF(next, wf_queue_node_t) != NULL) {
		int enq_tid = STAMPED_REF_TO_REF(next, wf_queue_node_t)->enq_tid;
		stamped_ref_t* op_stamped_ref = *(op_desc->ref_mem.ops + enq_tid);
		int old_stamp = op_stamped_ref->stamp;
		int new_stamp =  old_stamp + 1;
		wf_queue_op_desc_t *op = STAMPED_REF_TO_REF(op_stamped_ref, wf_queue_op_desc_t);
		#ifdef DEBUG
		found_tail_next_non_null[thread_id]++;
		#endif
		if ((STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t) == last)) {
		#ifdef DEBUG
			cond1_true[thread_id]++;
		#endif
		if  ((op->node == next->ref)) {
		#ifdef DEBUG
			can_update_tail[thread_id]++;
		#endif
			stamped_ref_t* op_new_stamped_ref = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));//*(op_desc->ref_mem.ops_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));
			op_new_stamped_ref->stamp = new_stamp;

			wf_queue_op_desc_t *op_new = (wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));//STAMPED_REF_TO_REF(op_new_stamped_ref, wf_queue_op_desc_t);//*(op_desc->ops_reserve + thread_id);//(wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));
			op_new_stamped_ref->ref = op_new;

			op_new->phase = op->phase;
			op_new->pending = false;
			op_new->enqueue = true;
			op_new->node = NULL;
			op_new->queue = queue;

			if (CAS_COND1(*(op_desc->ref_mem.ops + enq_tid), op_stamped_ref, op, old_stamp, op_new_stamped_ref)) {
				if (CAS_COND2(*(op_desc->ref_mem.ops + enq_tid), op_stamped_ref, op, old_stamp, op_new_stamped_ref)) {

				} else if (CAS_COND3(*(op_desc->ref_mem.ops + enq_tid), op_stamped_ref, op, old_stamp, op_new_stamped_ref)) {
					*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
				#ifdef DEBUG
					change[enq_tid][op->node->index].pending_off_ctr++;
					assert(change[enq_tid][op->node->index].pending_off_ctr == 1);
					change[enq_tid][op->node->index].pending_off = thread_id+1;
				#endif
				}
			}

			stamped_ref_t* node_new_stamped_ref = *(op_desc->ref_mem.tail_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));
			node_new_stamped_ref->ref = next->ref;
			node_new_stamped_ref->stamp = new_stamp_tail;
			if (CAS_COND1(queue->tail, old_stamped_ref_tail, last, old_stamp_tail, node_new_stamped_ref)) {
				#ifdef DEBUG
				    CAS_COND_1[thread_id]++;
				#endif
				if (CAS_COND2(queue->tail, old_stamped_ref_tail, last, old_stamp_tail, node_new_stamped_ref)) {
				#ifdef DEBUG
					CAS_COND_2[thread_id]++;
				#endif
				} else if (CAS_COND3(queue->tail, old_stamped_ref_tail, last, old_stamp_tail, node_new_stamped_ref)) {
					*(op_desc->ref_mem.tail_reserve + thread_id) = old_stamped_ref_tail;
				#ifdef DEBUG
					CAS_COND_3[thread_id]++;
					change[enq_tid][op->node->index].tail_update_ctr++;
					if (change[enq_tid][op->node->index].tail_update_ctr == 2) {
					    assert(change[enq_tid][op->node->index].tail_update_ctr == 1);

					}
					change[enq_tid][op->node->index].old_tail = old_stamped_ref_tail;
					change[enq_tid][op->node->index].old_ref = last;
					change[enq_tid][op->node->index].old_stamp = old_stamp_tail;
					change[enq_tid][op->node->index].new_tail = node_new_stamped_ref;
					change[enq_tid][op->node->index].new_ref = node_new_stamped_ref->ref;
					change[enq_tid][op->node->index].new_stamp = new_stamp_tail;

					change[enq_tid][op->node->index].tail_update = thread_id + 1;
				#endif
				}
			}
		}}
	}

	LOG_EPILOG();
}

void help_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase) {
	LOG_PROLOG();

	while (is_pending(op_desc, phase, thread_to_help)) {
		wf_queue_node_t *first = STAMPED_REF_TO_REF(queue->head, wf_queue_node_t);
		wf_queue_node_t *last = STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t);
		wf_queue_node_t *next = STAMPED_REF_TO_REF(first->next, wf_queue_node_t);
		if (first == queue->head->ref) {
			if (first == last) {
				if (next == NULL) {
					stamped_ref_t* op_stamped_ref = *(op_desc->ref_mem.ops + thread_to_help);
					int old_stamp = op_stamped_ref->stamp;
					int new_stamp = old_stamp + 1;
					wf_queue_op_desc_t *op_old = STAMPED_REF_TO_REF(op_stamped_ref, wf_queue_op_desc_t);
					if ((last == queue->tail->ref) && is_pending(op_desc, phase, thread_to_help)) {

						stamped_ref_t* op_new_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));
						op_new_stamped_ref->stamp = new_stamp;

						wf_queue_op_desc_t* op_new = STAMPED_REF_TO_REF(op_new_stamped_ref, wf_queue_op_desc_t);//*(op_desc->ops_reserve + thread_id);//(wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));
						op_new->phase = op_old->phase;
						op_new->enqueue = 0;
						op_new->pending = 0;
						op_new->node = NULL;
						op_new->queue = queue;
						if (CAS_COND1(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
							if (CAS_COND2(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
								// Nothing
							} else if (CAS_COND3(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
								*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
							}
						}
					}
				} else {
					help_finish_enq(queue, op_desc, thread_id);
				}
			} else {
				stamped_ref_t* op_stamped_ref = *(op_desc->ref_mem.ops + thread_to_help);
				int old_stamp = op_stamped_ref->stamp;
				int new_stamp = old_stamp + 1;
				wf_queue_op_desc_t *op_old = STAMPED_REF_TO_REF(op_stamped_ref, wf_queue_op_desc_t);
				if (!is_pending(op_desc, phase, thread_to_help)) {
					break;
				}

				if (first == queue->head->ref && op_old->node != first) {
					stamped_ref_t* op_new_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));
					op_new_stamped_ref->stamp = new_stamp;

					wf_queue_op_desc_t* op_new = STAMPED_REF_TO_REF(op_new_stamped_ref, wf_queue_op_desc_t);//*(op_desc->ops_reserve + thread_id);
					op_new->phase = op_old->phase;
					op_new->enqueue = 0;
					op_new->pending = 1;
					op_new->node = first;
					op_new->queue = queue;
					if (CAS_COND1(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
						if (CAS_COND2(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
							// Nothing
						} else if (CAS_COND3(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
							*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
						} else {
							continue;
						}
					} else {
						continue;
					}
				}

				compare_and_swap32(&(first->deq_tid), -1, thread_to_help);
				help_finish_deq(queue, op_desc, thread_id);
			}
		}
	}
	LOG_EPILOG();
}

void help_finish_deq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	stamped_ref_t *old_stamped_ref_head = queue->head;
	wf_queue_node_t *first = STAMPED_REF_TO_REF(queue->head, wf_queue_node_t);
	wf_queue_node_t *next = STAMPED_REF_TO_REF(first->next, wf_queue_node_t);
	int old_stamp_node = queue->head->stamp;
	int new_stamp_node = old_stamp_node + 1;
	int deq_id = first->deq_tid;
	if (deq_id != -1) {
		stamped_ref_t* op_stamped_ref = *(op_desc->ref_mem.ops + deq_id);
		int old_stamp = op_stamped_ref->stamp;
		int new_stamp = old_stamp + 1;
		wf_queue_op_desc_t *op_old = STAMPED_REF_TO_REF(op_stamped_ref, wf_queue_op_desc_t);
		if ((first == queue->head->ref) && (next != NULL)) {
			stamped_ref_t* op_new_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));
			op_new_stamped_ref->stamp = new_stamp;

			wf_queue_op_desc_t* op_new = STAMPED_REF_TO_REF(op_new_stamped_ref, wf_queue_op_desc_t);//*(op_desc->ops_reserve + thread_id);//(wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));
			op_new->phase = op_old->phase;
			op_new->enqueue = 0;
			op_new->pending = 0;
			op_new->node = op_old->node;
			op_new->queue = queue;
			if (CAS_COND1(*(op_desc->ref_mem.ops + deq_id), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
				if (CAS_COND2(*(op_desc->ref_mem.ops + deq_id), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
					// Nothing
				} else if (CAS_COND3(*(op_desc->ref_mem.ops + deq_id), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
					*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
				}
			}

			stamped_ref_t* node_new_stamped_ref = *(op_desc->ref_mem.head_reserve + thread_id);//(stamped_ref_t*)malloc(sizeof(stamped_ref_t));
			node_new_stamped_ref->ref = next;
			node_new_stamped_ref->stamp = new_stamp_node;
			if (CAS_COND1(queue->head, old_stamped_ref_head, first, old_stamp_node, node_new_stamped_ref)) {
				if (CAS_COND2(queue->head, old_stamped_ref_head, first, old_stamp_node, node_new_stamped_ref)) {
					// Nothing
				} else if (CAS_COND3(queue->head, old_stamped_ref_head, first, old_stamp_node, node_new_stamped_ref)) {
					*(op_desc->ref_mem.head_reserve + thread_id) = old_stamped_ref_head;
				}
			}
		}
	}

	LOG_EPILOG();
}

int wf_queue_count_nodes(wf_queue_head_t* head) {
	LOG_PROLOG();

	int count = 0;
	stamped_ref_t* tmp_head = head->head;
	stamped_ref_t* tmp_tail = head->tail;
	while (tmp_head->ref != NULL) {
		count++;
		tmp_head = STAMPED_REF_TO_REF(tmp_head, wf_queue_node_t)->next;
	}

	LOG_EPILOG();

	return count;
}
