#ifndef _CHUNK_H
#define _CHUNK_H

#define PAGE_SIZE 4096 // Page size = 4096 bytes
#define CHUNK_SIZE PAGE_SIZE // We consider a chunk's size = page size
#define MIN_BLOCK_SIZE 16 // Minimum memory(bytes) returned to caller
#define HEADER_SIZE 2 // Number of integers = 64 bits for 64 blocks

typedef struct {                                                                                              
	void* chunk;                                                                                              
} BlockHeader;

typedef struct {
	int allocation_data[HEADER_SIZE]; // Here we keep 1 bit for each block in chunk, 0 if block is used else 1
	int block_size; // What is the block size used in this chunk.
	int count_blocks; // Cache this value to avoid redundant computations.
} ChunkHeader;

typedef struct {
	ChunkHeader header;
	void* mem; // PAGE_SIZE-(memory allocated to header) bytes
} Chunk;

Chunk* CreateChunk(int block_size);				// Create a new chunk, allocate and initialize memory for
												// header and the blocks in the chunk.

void DestroyChunk(Chunk* chunk);				// Free all the memory corresponding to this chunk.

void* AllocateNthBlock(Chunk* chunk, int n);	// Return the pointer to the nth block's memory 
												// and set 0 in allocation data.

void* AllocateFirstFreeBlock(Chunk* chunk);		// Allocate the first free block and set 0 for it's 
												// allocation data in header.

void FreeNthBlock(Chunk* chunk, int n);			// Set 1 for the allocation data for the nth block.

void FreeBlock(void* block);					// Find the header for this block and set 1 in allocation 
												// data for that block, marking it available for reuse.
#endif
