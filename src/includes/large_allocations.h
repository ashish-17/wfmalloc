#ifndef LARGE_ALLOCATIONS_H_
#define LARGE_ALLOCATIONS_H_

#include<stdlib.h>
#include<stdint.h>

typedef struct lpage_header {
  uintptr_t num_pages; //Want this to be word-algned.  May break on exotic configurations, but this should handle most cases.
} lpage_header_t;

typedef struct lpage {
  lpage_header_t header;
  uint8_t payload[];
} lpage_t;

void* lalloc(size_t bytes);
void lfree(void* ptr);
uint32_t is_large_allocation(void* ptr);
size_t get_num_pages_from_payload(void* pptr);
#endif
