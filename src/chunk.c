#include <chunk.h>
#include <common.h>

// Local functions
int CalcNumberOfBlocks(int block_size);

Chunk* CreateChunk(int block_size) {
	LOG_PROLOG();

	Chunk* chunk = (Chunk*)malloc(CHUNK_SIZE);
	chunk->header.block_size = block_size;
	memset(chunk->header.allocation_data, 0, sizeof(int)*HEADER_SIZE);

	int offset = sizeof(Chunk);
	
	chunk->mem = ((char*)chunk + offset);
	
	int num_blocks = CalculateNumberOfBlocks(block_size);
	if (num_blocks==0) {
		LOG_ERROR("Invalid block size");
	}

	chunk->header.count_blocks = num_blocks;
	
	int tmp = 0;
	for (int i = 0; (i < HEADER_SIZE) && (num_blocks > 0) ; ++i) {
		if (num_blocks > sizeof(int)) {
			num_blocks = num_blocks - sizeof(int);
			chunk->header.allocation_data[i] = (~0);
		} else {
			chunk->header.allocation_data[i] = (~((~0)<<num_blocks));	
		}
	}

	LOG_EPILOG();
}

void DestroyChunk(Chunk* chunk) {
	LOG_PROLOG();

	free(chunk);

	LOG_EPILOG();
}

void* AllocateNthBlock(Chunk* chunk, int n) {
	LOG_PROLOG();

	if (n >= chunk->header.count_blocks) {
		LOG_ERROR("Invalid index of block");
	}

	int header_index = n / sizeof(int);
	int bit_index = n % sizeof(int);

	chunk->header.allocation_data[header_index] |= (1<<bit_index);

	char* block = (char*)chunk + (sizeof(Chunk) + ((chunk->header.block_size + sizeof(BlockHeader))*n));
	BlockHeader block_header = (BlockHeader)block;
	block->chunk = chunk;

	block += sizeof(BlockHeader);

	LOG_EPILOG();

	return (void)block;
}

int CalcNumberOfBlocks(int block_size) {
	LOG_PROLOG();

	int offset = sizeof(Chunk);
	int memory_for_block = block_size + sizeof(BlockHeader);                                                  
	int num_blocks = ((CHUNK_SIZE - offset) / memory_for_block);                                              

	LOG_EPILOG();

	return num_blocks;
}
