/*
 * queue.h
 *
 *  Created on: Dec 30, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_QUEUE_H_
#define INCLUDES_QUEUE_H_

#include <stdint.h>
#include<stdlib.h>

int *nodes_dequeued; //= (int*)malloc(sizeof(int) * 5000005);
struct wf_queue_node;

typedef struct wf_queue_node {
	struct wf_queue_node* next;
	int enq_tid;
	int deq_tid; // use CAS to update it
	#ifdef DEBUG
	int index;
	#endif
} wf_queue_node_t;

typedef struct wf_queue_head {
	volatile wf_queue_node_t* head;
	volatile wf_queue_node_t* tail;
} wf_queue_head_t;

/*
 * One for each thread to sync operations among threads.
 */
typedef struct wf_queues_op_desc {
	long phase;
	uint8_t pending;
	uint8_t enqueue;
	volatile wf_queue_node_t * node;
	wf_queue_head_t* queue;
} wf_queue_op_desc_t;

typedef struct wf_queues_op_head {
	int num_threads;
	wf_queue_op_desc_t** ops;
	wf_queue_op_desc_t** ops_reserve;
} wf_queue_op_head_t;

wf_queue_head_t* create_wf_queue(wf_queue_node_t* sentinel);

wf_queue_node_t* create_wf_queue_node();

void init_wf_queue_node(wf_queue_node_t* node);

wf_queue_op_head_t* create_queue_op_desc(int num_threads);

void wf_enqueue(wf_queue_head_t *q, wf_queue_node_t* node, wf_queue_op_head_t* op_desc, int thread_id);

wf_queue_node_t* wf_dequeue(wf_queue_head_t *q, wf_queue_op_head_t* op_desc, int thread_id);

int wf_queue_count_nodes(wf_queue_head_t* head);

#endif /* INCLUDES_QUEUE_H_ */
