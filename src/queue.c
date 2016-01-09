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
#include <pthread.h>

#define STAMPED_REF_TO_REF(ptr, ref_type) ((ref_type*)((ptr)->ref))
#define CAS(old_stamped_ref, expected_stamped_ref, expected_ref, expected_stamp, new_stamped_ref) \
	((((old_stamped_ref)->ref == (expected_ref)) && \
			((old_stamped_ref)->stamp == (expected_stamp))) && \
			((((old_stamped_ref)->ref == (new_stamped_ref)->ref) && \
					((old_stamped_ref)->stamp == (new_stamped_ref)->stamp)) || \
					(compare_and_swap_ptr (&(old_stamped_ref), (expected_stamped_ref), (new_stamped_ref)))))

#define CAS_OLD(old_stamped_ref, expected_stamped_ref, expected_ref, expected_stamp, new_stamped_ref) \
	(compare_and_swap_ptr (&(old_stamped_ref), (expected_stamped_ref), (new_stamped_ref)))

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
		(*(op_desc->ref_mem.ops_reserve + thread))->ref = NULL;
	}

	LOG_EPILOG();
	return op_desc;
}

void wf_enqueue(wf_queue_head_t *q, wf_queue_node_t* node, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	long phase = max_phase(op_desc) + 1;
	stamped_ref_t *old_stamped_ref = *(op_desc->ref_mem.ops + thread_id);
	stamped_ref_t *reserve_stamped_ref = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));//*(op_desc->ref_mem.ops_reserve + thread_id);

	LOG_INFO("Before enqueue old_stamped_ref = %x reserve_stamped_ref = %x", old_stamped_ref, reserve_stamped_ref);

	wf_queue_op_desc_t *op = (wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));//*(op_desc->ops_reserve + thread_id);
	op->phase = phase;

	node->enq_tid = thread_id;
	node->next->ref = NULL;

	op->node = node;
	op->pending = 1;
	op->enqueue = 1;

	reserve_stamped_ref->ref = op;
	reserve_stamped_ref->stamp = old_stamped_ref->stamp;

	*(op_desc->ref_mem.ops_reserve + thread_id) = old_stamped_ref;
	*(op_desc->ops_reserve + thread_id) = old_stamped_ref->ref;

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
	stamped_ref_t *reserve_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);

	LOG_INFO("Before dequeue old_stamped_ref = %x reserve_stamped_ref = %x", old_stamped_ref, reserve_stamped_ref);

	wf_queue_op_desc_t *op = *(op_desc->ops_reserve + thread_id);
	op->phase = phase;
	op->node = NULL;
	op->pending = 1;
	op->enqueue = false;

	reserve_stamped_ref->ref = op;
	reserve_stamped_ref->stamp = old_stamped_ref->stamp;

	*(op_desc->ref_mem.ops_reserve + thread_id) = old_stamped_ref;
	*(op_desc->ops_reserve + thread_id) = old_stamped_ref->ref;

	*(op_desc->ref_mem.ops + thread_id) = reserve_stamped_ref;

	help(q, op_desc, thread_id, phase);
	help_finish_deq(q, op_desc, thread_id);

	node = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread_id), wf_queue_op_desc_t)->node;
	if (node == NULL) {
		LOG_WARN("Dequeued node is NULL");
	}

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
			if (op_tmp->enqueue == 1) {
				help_enq(queue, op_desc, thread_id, thread, phase);
			} else {
				help_deq(queue, op_desc, thread_id, thread, phase);
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

	return max_phase;
}

bool is_pending(wf_queue_op_head_t* op_desc, long phase, int thread_id) {
	LOG_PROLOG();

	LOG_INFO("Pendng - thread %d", pthread_self());

	wf_queue_op_desc_t* op_tmp = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread_id), wf_queue_op_desc_t);

	LOG_EPILOG();
	return ((op_tmp->pending == 1) && (op_tmp->phase <= phase));
}

void help_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, int thread_to_help, long phase) {
	LOG_PROLOG();

	while (is_pending(op_desc, phase, thread_to_help)) {
		wf_queue_node_t *last = STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t);
		stamped_ref_t *next = last->next;
		wf_queue_node_t *new_node = STAMPED_REF_TO_REF(*(op_desc->ref_mem.ops + thread_to_help), wf_queue_op_desc_t)->node;

		uint32_t old_stamp = last->next->stamp;
		uint32_t new_stamp = (old_stamp + 1);
		if (last == STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t)) {
			if (STAMPED_REF_TO_REF(last->next, wf_queue_node_t) == NULL) {
				if (is_pending(op_desc, phase, thread_to_help)) {
					stamped_ref_t* node_new_stamped_ref = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));//*(op_desc->ref_mem.next_reserve + thread_id);
					node_new_stamped_ref->ref = new_node;
					node_new_stamped_ref->stamp = new_stamp;
					if (CAS(STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t)->next, next, next->ref, next->stamp, node_new_stamped_ref)) {
						*(op_desc->ref_mem.next_reserve + thread_id) = next; // Recycle stamped reference
						help_finish_enq(queue, op_desc, thread_id);
						LOG_INFO("After CAS help_enq");
						return;
					} else {
						LOG_INFO("CAS failed on OP_DESC1");
					}
				}
			} else {
				help_finish_enq(queue, op_desc, thread_id);
				LOG_INFO("1");
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
		LOG_INFO("2");
		int enq_tid = STAMPED_REF_TO_REF(next, wf_queue_node_t)->enq_tid;
		stamped_ref_t* op_stamped_ref = *(op_desc->ref_mem.ops + enq_tid);
		int old_stamp = op_stamped_ref->stamp;
		int new_stamp =  old_stamp + 1;
		wf_queue_op_desc_t *op = STAMPED_REF_TO_REF(op_stamped_ref, wf_queue_op_desc_t);
		LOG_INFO("%x %x", op->node, next->ref);
		if ((STAMPED_REF_TO_REF(queue->tail, wf_queue_node_t) == last) && (op->node == next->ref)) {
			LOG_INFO("3");
			wf_queue_op_desc_t *op_new = (wf_queue_op_desc_t*)malloc(sizeof(wf_queue_op_desc_t));//*(op_desc->ops_reserve + thread_id);
			op_new->phase = op->phase;
			op_new->pending = false;
			op_new->enqueue = true;
			op_new->node = op->node;

			stamped_ref_t* op_new_stamped_ref = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));//*(op_desc->ref_mem.ops_reserve + thread_id);
			op_new_stamped_ref->ref = op_new;
			op_new_stamped_ref->stamp = new_stamp;
			LOG_INFO("new stamp op_desc= %d", new_stamp);
			LOG_INFO("Before OP_DESC2 CAS 1 = %x 2 = %x", *(op_desc->ref_mem.ops + enq_tid), op_stamped_ref);
			if (CAS(*(op_desc->ref_mem.ops + enq_tid), op_stamped_ref, op, old_stamp, op_new_stamped_ref)) {
				*(op_desc->ops_reserve + thread_id) = op;
				*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
				LOG_INFO("(Thread - %d) After OP_DESC2 CAS 1 = %x 2 = %x", thread_id, *(op_desc->ref_mem.ops + enq_tid), op_stamped_ref);
				LOG_INFO("CAS success on OP_DESC2");
			} else {
				LOG_INFO("CAS failed on OP_DESC2");
			}

			stamped_ref_t* node_new_stamped_ref = (stamped_ref_t*)malloc(sizeof(stamped_ref_t));//*(op_desc->ref_mem.tail_reserve + thread_id);
			node_new_stamped_ref->ref = next->ref;
			node_new_stamped_ref->stamp = new_stamp_tail;
			LOG_INFO("new stamp tail= %d", new_stamp_tail);
			if (CAS(queue->tail, old_stamped_ref_tail, last, old_stamp_tail, node_new_stamped_ref)) {
				*(op_desc->ref_mem.tail_reserve + thread_id) = old_stamped_ref_tail;
				LOG_INFO("CAS success on tail");
			} else {
				LOG_INFO("CAS failed on tail");
			}
		}
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
						wf_queue_op_desc_t* op_new = *(op_desc->ops_reserve + thread_id);
						op_new->phase = op_old->phase;
						op_new->enqueue = 0;
						op_new->pending = 0;
						op_new->node = NULL;
						stamped_ref_t* op_new_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);
						op_new_stamped_ref->ref = op_new;
						op_new_stamped_ref->stamp = new_stamp;
						if (CAS(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
							*(op_desc->ops_reserve + thread_id) = op_old;
							*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
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
					wf_queue_op_desc_t* op_new = *(op_desc->ops_reserve + thread_id);
					op_new->phase = op_old->phase;
					op_new->enqueue = 0;
					op_new->pending = 1;
					op_new->node = first;
					stamped_ref_t* op_new_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);
					op_new_stamped_ref->ref = op_new;
					op_new_stamped_ref->stamp = new_stamp;
					if (CAS(*(op_desc->ref_mem.ops + thread_to_help), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
						*(op_desc->ops_reserve + thread_id) = op_old;
						*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
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
			wf_queue_op_desc_t* op_new = *(op_desc->ops_reserve + thread_id);
			op_new->phase = op_old->phase;
			op_new->enqueue = 0;
			op_new->pending = 0;
			op_new->node = op_old->node;
			stamped_ref_t* op_new_stamped_ref = *(op_desc->ref_mem.ops_reserve + thread_id);
			op_new_stamped_ref->ref = op_new;
			op_new_stamped_ref->stamp = new_stamp;
			if (CAS(*(op_desc->ref_mem.ops + deq_id), op_stamped_ref, op_old, old_stamp, op_new_stamped_ref)) {
				*(op_desc->ops_reserve + thread_id) = op_old;
				*(op_desc->ref_mem.ops_reserve + thread_id) = op_stamped_ref;
			}

			stamped_ref_t* node_new_stamped_ref = *(op_desc->ref_mem.head_reserve + thread_id);
			node_new_stamped_ref->ref = next;
			node_new_stamped_ref->stamp = new_stamp_node;
			if (CAS(queue->head, old_stamped_ref_head, first, old_stamp_node, node_new_stamped_ref)) {
				*(op_desc->ref_mem.head_reserve + thread_id) = old_stamped_ref_head;
			}
		}
	}

	LOG_EPILOG();
}
