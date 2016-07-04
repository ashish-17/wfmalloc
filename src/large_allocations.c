#include "includes/config.h"
#include "includes/large_allocations.h"
#include "includes/utils.h"
#include "includes/logger.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <assert.h>


int is_lpage_aligned (void* lptr) {
    return ((uintptr_t)lptr % PAGE_SIZE == 0);
}

lpage_t* get_lptr(void *pptr) {
    return (lpage_t*)((uintptr_t)pptr - sizeof(lpage_header_t));
}	

void set_num_pages(lpage_t *lptr, uint32_t num_pages) {
    lptr->header.num_pages = num_pages;
}

size_t get_num_pages(lpage_t *lptr) {
    return lptr->header.num_pages;
}

size_t get_num_pages_from_payload(void* pptr) {
    return get_num_pages(get_lptr(pptr));
}

void* get_payload_ptr(lpage_t* lptr) {
    return lptr->payload; 
}

uint32_t is_large_allocation(void* ptr) {
    return(is_lpage_aligned(get_lptr(ptr)));
}


void* lalloc(size_t size) {

    LOG_PROLOG();
    
    assert(size > MAX_BLOCK_SIZE);
    size += sizeof(lpage_header_t);

    size_t round_up_size = (size + (PAGE_SIZE - 1)) & PAGE_MASK;
    uint32_t num_pages = round_up_size / PAGE_SIZE;

    void* ptr = mmap(NULL, round_up_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    //printf("%p %u %u %u\n", ptr, size, round_up_size, num_pages);
    //perror("mmap");
    assert(ptr != (void*) -1);
    assert(is_lpage_aligned(ptr));

    set_num_pages(ptr, num_pages);
        
    LOG_EPILOG();
    return get_payload_ptr(ptr);
}

void lfree(void* ptr) {

	LOG_PROLOG();
	lpage_t *lptr = get_lptr(ptr);
	assert(is_lpage_aligned(lptr));
	uint32_t num_pages = get_num_pages(lptr);
	munmap(ptr, num_pages * PAGE_SIZE);
	LOG_EPILOG();
}

