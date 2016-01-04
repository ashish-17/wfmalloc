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

void help(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase);
long max_phase(wf_queue_op_head_t* op_desc);
bool is_pending(wf_queue_op_head_t* op_desc, long phase, int thread_id);
void help_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase);
void help_finish_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc);

wf_queue_head_t* create_wf_queue() {
	LOG_PROLOG();

	wf_queue_head_t* queue = (wf_queue_head_t*)malloc(sizeof(wf_queue_head_t));
	queue->head = create_wf_queue_node();
	queue->tail = queue->head;

	LOG_EPILOG();
	return queue;
}

void init_wf_queue_node(wf_queue_node_t* node) {
	LOG_PROLOG();

	node->next = NULL;
	node->stamp = 0;
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

	LOG_EPILOG();
	return op_desc;
}

void wf_enqueue(wf_queue_head_t *q, wf_queue_node_t* node, wf_queue_op_head_t* op_desc, int thread_id) {
	LOG_PROLOG();

	long phase = max_phase(op_desc) + 1;
	wf_queue_op_desc_t *op = *(op_desc->ops_reserve + thread_id);
	op->phase = phase;

	node->enq_tid = thread_id;
	node->next = NULL;
	op->node = node;

	op->pending = 1;
	op->enqueue = 1;

	wf_queue_op_desc_t* op_old = *(op_desc->ops + thread_id);
	*(op_desc->ops + thread_id) = op;
	*(op_desc->ops_reserve + thread_id) = op_old;

	help(q, op_desc, thread_id, phase);
	help_finish_enq(q, op_desc);

	LOG_EPILOG();
}

void help(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase) {
	LOG_PROLOG();

	int thread = 0;
	int num_threads = op_desc->num_threads;
	wf_queue_op_desc_t* op_tmp = NULL;
	for (thread = 0; thread < num_threads; ++thread) {
		op_tmp = *(op_desc->ops + thread);
		if ((op_tmp->pending == 1) && (op_tmp->phase <= phase)) {
			if (op_tmp->enqueue == 1) {
				help_enq(queue, op_desc, thread, phase);
			} else {
				LOG_ERROR("Dequeue not implemented yet!");
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
		phase = (*(op_desc->ops + thread))->phase;
		if (phase > max_phase) {
			max_phase = phase;
		}
	}

	return max_phase;
}

bool is_pending(wf_queue_op_head_t* op_desc, long phase, int thread_id) {
	LOG_PROLOG();

	wf_queue_op_desc_t* op_tmp = *(op_desc->ops + thread_id);

	LOG_EPILOG();
	return ((op_tmp->pending == 1) && (op_tmp->phase <= phase));
}

void help_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc, int thread_id, long phase) {
	LOG_PROLOG();

	while (is_pending(op_desc, phase, thread_id)) {
		wf_queue_node_t *last = queue->tail;
		wf_queue_node_t *next = last->next;
		wf_queue_node_t *new_node = (*(op_desc->ops + thread_id))->node;
		uint32_t old_stamp = queue->tail->stamp;
		uint32_t new_stamp = (old_stamp + 1);
		new_node->stamp = new_stamp;
		if (last == queue->tail) {
			if (next == NULL) {
				if (is_pending(op_desc, phase, thread_id)) {
					if ((queue->tail->next == next) && (queue->tail->stamp == old_stamp) &&
							(((queue->tail->next == new_node) && (queue->tail->stamp == new_stamp)) || compare_and_swap_ptr(&(last->next), next, new_node))) {
						help_finish_enq(queue, op_desc);
						return;
					}
				}
			} else {
				help_finish_enq(queue, op_desc);
			}
		}
	}
	LOG_EPILOG();
}

void help_finish_enq(wf_queue_head_t* queue, wf_queue_op_head_t* op_desc) {
	LOG_PROLOG();

	wf_queue_node_t *last = queue->tail;
	wf_queue_node_t *next = last->next;
	if (next != NULL) {
		int enq_tid = next->enq_tid;
		wf_queue_op_desc_t *op = (*(op_desc->ops + enq_tid));
		if ((queue->tail == last) && ((*(op_desc->ops + enq_tid))->node == next)) {
			wf_queue_op_desc_t *op_new = *(op_desc->ops_reserve + enq_tid);
			op_new->phase = op->phase;
			op_new->pending = false;
			op_new->enqueue = true;
			op_new->node = next;

			if (compare_and_swap_ptr((op_desc->ops + enq_tid), op, op_new)) {
				*(op_desc->ops_reserve + enq_tid) = op;
			}

			compare_and_swap_ptr(&(queue->tail), last, next);
		}
	}

	LOG_EPILOG();
}
