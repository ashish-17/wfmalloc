/*
 * page_heap.c
 *
 *  Created on: Jul 27, 2016
 *      Author: ashish
 */

#include "includes/page_heap.h"
#include "includes/utils.h"
#include "includes/logger.h"
#include <stdlib.h>
#include <string.h>

const int MAX_TRY = 3;

uint32_t page_count_to_bin_idx(uint32_t page_count);
uint32_t mem_size_to_bin(uint32_t size);
uint32_t get_next_alloc_count(uint32_t current_count, uint32_t bin);
void* alloc_heap_node(uint32_t page_count);
void* alloc_n_heap_nodes(uint32_t page_count, uint32_t n);

page_heap_t* create_page_heap(int count_threads) {
	LOG_PROLOG();

	page_heap_t* heap = (page_heap_t*)malloc(sizeof(page_heap_t));
	heap->count_threads = count_threads;
	heap->num_alloc_nodes = (uint32_t*)malloc(COUNT_PAGE_HEAP_BINS*sizeof(int));
	heap->count_malloc = (uint32_t*)malloc(sizeof(uint32_t)*heap->count_threads);
	heap->count_steal = (uint32_t*)malloc(sizeof(uint32_t)*heap->count_threads);
	memset(heap->count_malloc, 0, sizeof(uint32_t)*heap->count_threads);
	memset(heap->count_steal, 0, sizeof(uint32_t)*heap->count_threads);
	int bin = 0;
	mem_block_header_t* node = NULL;
	for (bin = 0; bin < COUNT_PAGE_HEAP_BINS; ++bin) {
		node = (mem_block_header_t*)alloc_n_heap_nodes(bin + 1, 1);
		heap->heap_data[bin] = create_wf_queue(&(node->wf_node));

		heap->op_desc[bin] = create_queue_op_desc(count_threads);
		heap->num_alloc_nodes[bin] = 2;
	}

	LOG_EPILOG();
	return heap;
}

mem_block_header_t* get_n_pages_cont(page_heap_t* ph, uint32_t n, int thread_id) {
	LOG_PROLOG();
	uint32_t bin = page_count_to_bin_idx(n);
	uint32_t count_tries = 0;
	wf_queue_node_t *node = NULL;
	mem_block_header_t* mem = NULL;
	while ((node == NULL) &&
			(count_tries < MAX_TRY) &&
			(bin < COUNT_PAGE_HEAP_BINS)) {
		node = wf_dequeue(ph->heap_data[bin], ph->op_desc[bin], thread_id);
		if (node == NULL) {
			bin++;
			count_tries++;
		}
	}

	if (node == NULL) {
		bin = page_count_to_bin_idx(n);
		uint32_t temp = ph->num_alloc_nodes[bin];
		mem_block_header_t *mem_new = (mem_block_header_t*)alloc_n_heap_nodes(n, temp);
		ph->num_alloc_nodes[bin] = get_next_alloc_count(ph->num_alloc_nodes[bin], bin);
		ph->count_malloc[thread_id]++;
		mem = mem_new;
		if (temp > 1) {
			mem_new = (mem_block_header_t*)list_entry(mem_new->wf_node.next, mem_block_header_t, wf_node);
			uint32_t new_bin = mem_size_to_bin(mem_new->size);
			wf_enqueue(ph->heap_data[new_bin], &(mem_new->wf_node), ph->op_desc[new_bin], thread_id);
		}
	} else if (count_tries > 0) {
		mem_block_header_t *mem_new = (mem_block_header_t*)list_entry(node, mem_block_header_t, wf_node);
		mem = mem_new;
		mem_new = (mem_block_header_t*)(((char*)mem_new + n * (PAGE_SIZE + sizeof(mem_block_header_t))));
		mem_new->size = (mem->size / PAGE_SIZE - n) * PAGE_SIZE;
		mem->size = (n * PAGE_SIZE);
		uint32_t new_bin = mem_size_to_bin(mem_new->size);
		wf_enqueue(ph->heap_data[new_bin], &(mem_new->wf_node), ph->op_desc[new_bin], thread_id);
		ph->count_steal[thread_id]++;
	} else {
		mem = (mem_block_header_t*)list_entry(node, mem_block_header_t, wf_node);
	}

	LOG_EPILOG();
	return mem;
}

void* get_n_pages(page_heap_t* heap, uint32_t n, int thread_id) {
	LOG_PROLOG();
	// May be later
	LOG_EPILOG();
	return NULL;
}

void add_to_page_heap(page_heap_t* ph, mem_block_header_t* mem, int thread_id) {
	LOG_PROLOG();

	uint32_t bin = mem_size_to_bin(mem->size);
	wf_enqueue(ph->heap_data[bin], &(mem->wf_node), ph->op_desc[bin], thread_id);
	LOG_EPILOG();
}

uint32_t get_next_alloc_count(uint32_t current_count, uint32_t bin) {
	LOG_PROLOG();
	uint32_t count = current_count;
	if (((current_count*2) * (bin + 1) * (PAGE_SIZE + sizeof(mem_block_header_t))) <= MAX_MALLOC_MEM) {
		count = current_count*2;
	} else if (((current_count + 1) * (bin + 1) * (PAGE_SIZE + sizeof(mem_block_header_t))) <= MAX_MALLOC_MEM) {
		count = current_count;
	} else {
		count = current_count;
	}
	LOG_EPILOG();
	return count;
}
uint32_t mem_size_to_bin(uint32_t size) {
	LOG_PROLOG();
	uint32_t page_count = size / PAGE_SIZE;
	LOG_EPILOG();
	return page_count_to_bin_idx(page_count);
}

uint32_t page_count_to_bin_idx(uint32_t page_count) {
	if (page_count <= COUNT_PAGE_HEAP_BINS) {
		return (page_count-1);
	} else {
		return COUNT_PAGE_HEAP_BINS-1;
	}
}

void* alloc_heap_node(uint32_t page_count) {
	mem_block_header_t* mem = (mem_block_header_t*)malloc(page_count * (PAGE_SIZE + sizeof(mem_block_header_t)));
	mem->size = (page_count * PAGE_SIZE);

	init_wf_queue_node(&(mem->wf_node));

	return mem;
}

void* alloc_n_heap_nodes(uint32_t page_count, uint32_t n) {
	void* mem = malloc(n * page_count * (PAGE_SIZE + sizeof(mem_block_header_t)));
	if (mem == NULL) {
		LOG_ERROR("OUT OF MEMORY!! (%d bytes)", n * page_count * (PAGE_SIZE + sizeof(mem_block_header_t)));
		return NULL;
	}

	uint32_t node_idx = 0;

	mem_block_header_t* tmp = NULL;
	mem_block_header_t* prev = NULL;
	for (node_idx = 0; node_idx < n; ++node_idx) {
		tmp = (mem_block_header_t*)((char*)mem + node_idx * page_count * (PAGE_SIZE + sizeof(mem_block_header_t)));
		tmp->size = (page_count * PAGE_SIZE);

		init_wf_queue_node(&(tmp->wf_node));
		if (prev != NULL) {
			prev->wf_node.next = &(tmp->wf_node);
		}

		prev = tmp;
	}

	return mem;
}
