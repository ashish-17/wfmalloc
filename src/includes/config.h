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

#define MAX_BINS 8 // 4 8 16 32 64 128 256 512
#define MIN_PAGES_PER_BIN 1


#endif /* INCLUDES_CONFIG_H_ */
