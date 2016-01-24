/*
 * wfmalloc.h
 *
 *  Created on: Jan 17, 2016
 *      Author: ashish
 */

#ifndef WFMALLOC_H_
#define WFMALLOC_H_

#include <stdlib.h>

void wfinit(int max_count_threads);

void* wfmalloc(size_t bytes, int thread_id);

void wffree(void* ptr);

void wfstats();

#endif /* WFMALLOC_H_ */
