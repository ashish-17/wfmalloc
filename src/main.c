#include <stdio.h>
#include <stdlib.h>
#include "includes/utils.h"
#include "includes/logger.h"
#include "includes/page.h"
#include "includes/local_pool.h"

void test_page() {
    LOG_PROLOG();

    page_header_t header;
    LOG_INFO("Size of header = %d", sizeof(header));
    LOG_INFO("Size of bitmap = %d", sizeof(header.block_flags));

    LOG_INFO("Address header = %u", &header);
    LOG_INFO("Offset block size = %u", OFFSETOF(page_header_t, block_size));
    LOG_INFO("Offset max blocks = %u", OFFSETOF(page_header_t, max_blocks));
    LOG_INFO("Offset bitmap = %u", OFFSETOF(page_header_t, block_flags));

    page_t *ptr = create_page(4);
    LOG_INFO("test page block size = %d", ptr->header.block_size);
    LOG_INFO("test page num blocks = %d", ptr->header.max_blocks);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));

    void* blk1 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    void* blk2 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    void* blk3 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    void* blk4 = malloc_block(ptr);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));

    free_block(blk3);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    free_block(blk4);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    free_block(blk2);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));
    free_block(blk1);
    LOG_INFO("first empty block = %d", find_first_empty_block(ptr));
    LOG_INFO("count empty blocks = %d", count_empty_blocks(ptr));

    LOG_EPILOG();
}

void test_local_pool() {
    LOG_PROLOG();

    local_pool_t* pool = create_local_pool();

    void* block = NULL;
	int i = 0;
	int count4 = 0, count8 = 0, count256 = 0, count512=0;
	for (i = 0; i < MAX_BLOCKS_IN_PAGE; ++i) {
		block = malloc_block_from_pool(pool, 1);
		if (block != NULL) {
			count4++;
		}

		block = malloc_block_from_pool(pool, 5);
		if (block != NULL) {
			count8++;
		}
		block = malloc_block_from_pool(pool, 253);
		if (block != NULL) {
			count256++;
		}

		block = malloc_block_from_pool(pool, 260);
		if (block != NULL) {
			count512++;
		}
	}

    LOG_INFO("Num blocks allocated: 4 bytes = %d, 8 bytes = %d, 256 bytes = %d, 512 bytes = %d", count4, count8, count256, count512);
    LOG_EPILOG();
}

int main() {
    LOG_INIT_CONSOLE();
    LOG_INIT_FILE();

    //test_page();
    test_local_pool();

    LOG_CLOSE();
    return 0;
}
