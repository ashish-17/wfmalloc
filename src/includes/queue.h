/*
 * queue.h
 *
 *  Created on: Dec 30, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_QUEUE_H_
#define INCLUDES_QUEUE_H_

#include <stdint.h>

extern int (*stop_world)(int);

typedef struct stamped_ref {
	void* ref;
	int stamp;
} stamped_ref_t;

typedef struct wf_thread_reference_memory {
	stamped_ref_t** next_reserve;
	stamped_ref_t** head_reserve;
	stamped_ref_t** tail_reserve;

	stamped_ref_t** ops;
	stamped_ref_t** ops_reserve;
} wf_thread_reference_memory_t ;

#define REF_MEM_NEXT_RES 0
#define REF_MEM_HEAD_RES 1
#define REF_MEM_TAIL_RES 2
#define REF_MEM_OPS 3
#define REF_MEM_OPS_RES 4

#define REF_MEM_COUNT (REF_MEM_OPS_RES+1)

typedef struct wf_queue_node {
	stamped_ref_t* next;
	int enq_tid;
	int deq_tid; // use CAS to update it
#ifdef DEBUG
	int index;
#endif
} wf_queue_node_t;

typedef struct wf_queue_head {
	volatile stamped_ref_t* head;
	volatile stamped_ref_t* tail;
} wf_queue_head_t;

/*
 * One for each thread to sync operations among threads.
 */
typedef struct wf_queues_op_desc {
	long phase;
	uint8_t pending;
	uint8_t enqueue;
	wf_queue_node_t * node;
	wf_queue_head_t* queue;
} wf_queue_op_desc_t;

typedef struct wf_queues_op_head {
	int num_threads;
	wf_queue_op_desc_t** ops;
	wf_queue_op_desc_t** ops_reserve;
	wf_thread_reference_memory_t ref_mem;
} wf_queue_op_head_t;

wf_queue_head_t* create_wf_queue(wf_queue_node_t* sentinel);

wf_queue_node_t* create_wf_queue_node();

void init_wf_queue_node(wf_queue_node_t* node);

wf_queue_op_head_t* create_queue_op_desc(int num_threads);

void wf_enqueue(wf_queue_head_t *q, wf_queue_node_t* node, wf_queue_op_head_t* op_desc, int thread_id);

wf_queue_node_t* wf_dequeue(wf_queue_head_t *q, wf_queue_op_head_t* op_desc, int thread_id);

int wf_queue_count_nodes(wf_queue_head_t* head);
# ifdef DEBUG
void check_state(int n_threads, wf_queue_op_head_t* op_desc);
# endif

#endif /* INCLUDES_QUEUE_H_ */
