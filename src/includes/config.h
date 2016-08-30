/*
 * configuration.h
 *
 *  Created on: Dec 26, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_CONFIG_H_
#define INCLUDES_CONFIG_H_

#define PAGE_SIZE 4096
#define BLOCK_HEADER_SIZE 8
#define MIN_BLOCK_SIZE 4
#define MAX_BLOCKS_IN_PAGE ((PAGE_SIZE) / (BLOCK_HEADER_SIZE + MIN_BLOCK_SIZE))
#define BLOCK_SIZE(blk_size) (BLOCK_HEADER_SIZE + blk_size)

#define BLOCK_EMPTY 0
#define BLOCK_OCCUPIED 1

#define MAX_BINS 10 // 4 8 16 32 64 128 256 512 1024 2048
static const long long TOTAL_INIT_MEMORY = 2147483648/2048; // 2GB
#define MIN_PAGES_PER_BIN 1

#define MIN_BLOCKS_RUN 128
#define MAX_BLOCKS_RUN 256
#define THRESHOLD_OVERFLOW_BIN (MAX_BLOCKS_RUN*256)

#define MAX_MLFQ 4
#define MLFQ_THRESHOLD 2

#define MAX_MALLOC_MEM 536870912 // Bytes

#define CALC_MLFQ_IDX(empty, max) ((empty) == (max) ? 0 : ((empty) == 0) ? ((MAX_MLFQ) - 1) : ((MAX_MLFQ-1) - (((empty) * (MAX_MLFQ-1)) / (max)) - 1))
#define MIN_OPS_BEFORE_REMOVAL 4


#endif /* INCLUDES_CONFIG_H_ */
