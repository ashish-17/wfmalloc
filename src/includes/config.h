/*
 * configuration.h
 *
 *  Created on: Dec 26, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_CONFIG_H_
#define INCLUDES_CONFIG_H_

#define WORD_SIZE = 8

#define PAGE_BITS 12 //Number of bits to index within a page, or log_2(PAGE_SIZE).
#define PAGE_SIZE (1 << PAGE_BITS)
#define PAGE_MASK (~(PAGE_SIZE - 1)) // with respect to PAGE_SIZE
#define BLOCK_HEADER_SIZE 8
#define MIN_BLOCK_SIZE 4
//#define MAX_BLOCKS_IN_PAGE ((PAGE_SIZE) / (BLOCK_HEADER_SIZE + MIN_BLOCK_SIZE))
#define MAX_BLOCKS_IN_PAGE ((PAGE_SIZE) / (MIN_BLOCK_SIZE))
#define BLOCK_SIZE(blk_size) (BLOCK_HEADER_SIZE + blk_size)

#define BLOCK_EMPTY 0
#define BLOCK_OCCUPIED 1

#define MAX_BINS 8 // 4 8 16 32 64 128 256 512
#define MAX_BLOCK_SIZE (1 << (MAX_BINS + 1))
static const long long TOTAL_INIT_MEMORY = 2147483648/2048; // 2GB
#define INIT_PAGES_PER_BIN(count_threads) (((((TOTAL_INIT_MEMORY / count_threads) / 2) / 8) / PAGE_SIZE) + 1)
#define MIN_PAGES_PER_BIN 8
#define NPAGES_PER_ALLOC 8 // no of pages to fetch from OS in one go on finding shared pool empty


#define MAX_MLFQ 4
#define MLFQ_THRESHOLD 2

#define CALC_MLFQ_IDX(empty, max) ((empty) == (max) ? 0 : ((empty) == 0) ? ((MAX_MLFQ) - 1) : ((MAX_MLFQ-1) - (((empty) * (MAX_MLFQ-1)) / (max)) - 1))
#define MIN_OPS_BEFORE_REMOVAL 4


#endif /* INCLUDES_CONFIG_H_ */
