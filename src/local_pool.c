#include "includes/local_pool.h"
#include "includes/utils.h"
#include "includes/logger.h"
#include <stdlib.h>
#include <unistd.h>

#ifdef LOG_LEVEL_STATS
#include <sys/time.h>
#endif

#include <assert.h>

// double pointer for head because we want to update it
void add_at_head(list_t *list, list_t **tail, page_t *page) {
	if (list_empty(list)) {
	    *tail = &page->header.node;
	}
	list_add_tail(&page->header.node, list);
}

void add_at_tail(list_t *list, list_t ** tail, page_t *page) {
	list_add_tail(&page->header.node, *tail);
	*tail = &page->header.node;
}

page_t* remove_from_head(list_t *list, list_t ** tail) {
	if (list_empty(list)) {
	    return NULL;
	} else {
	    list_t *temp = list->prev;
	
	    // TODO: might be next as well
	    list_del(temp);
	    page_t *page = (page_t*)list_entry(temp, page_header_t, node);
	    if (list_empty(list)) {
	        *tail = list;
	    }
	    return page;
	}
}

page_t* remove_from_tail(list_t *list, list_t ** tail) {
	if (list_empty(list)) {
            return NULL;
        } else {
            list_del(*tail);
            page_t *page = (page_t*)list_entry(*tail, page_header_t, node);
            *tail = (*tail)->next;
            return page;
        }	
}

local_pool_t* create_local_pool(int count_threads) {
	LOG_PROLOG();
        
        int num_processors = sysconf( _SC_NPROCESSORS_ONLN);
        
        local_pool_t *pool = (local_pool_t*) malloc(sizeof(local_pool_t));
        pool->count_processors = num_processors;
        pool->count_threads = count_threads;
        pool->init_pages_per_bin = INIT_PAGES_PER_BIN(count_threads);
	pool->min_pages_per_bin = MIN_PAGES_PER_BIN;
	pool->thread_data = (local_thread_data_t*)malloc(sizeof(local_thread_data_t) * count_threads);

	int thread = 0, bin = 0, mlfq = 0, page_idx = 0;
        local_thread_data_t* thread_data = NULL;
        int block_size = MIN_BLOCK_SIZE;
	page_t* ptr_page = NULL;

	for (thread = 0; thread < count_threads; ++thread) {
		thread_data = (pool->thread_data + thread);
                block_size = MIN_BLOCK_SIZE;
		for (bin = 0; bin < MAX_BINS; ++bin) {
			for (mlfq = 0; mlfq < MAX_MLFQ; ++mlfq) {
                                INIT_LIST_HEAD(&(thread_data->bins[bin][mlfq])); 
				thread_data->tail[bin][mlfq] = &thread_data->bins[bin][mlfq];
                        }

			for (page_idx = 0; page_idx < pool->init_pages_per_bin; page_idx++) {
				ptr_page = create_npages_aligned(block_size, 1);
				add_at_tail(&((pool->thread_data + thread)->bins[bin][MAX_MLFQ-1]), &((pool->thread_data + thread)->tail[bin][MAX_MLFQ - 1]), ptr_page);  // since all the blocks are available
			}

			block_size *= 2;
			thread_data->page_cache[bin] = NULL;
			//thread_data->last_shared_pool_idx = 0;
			thread_data->total_pages[bin] = pool->min_pages_per_bin;
			thread_data->mlfq_to_assess = 0;
		}

		#ifdef LOG_LEVEL_STATS
			thread_data->count_malloc = 0;
			thread_data->count_request_shared_pool = 0;
                        thread_data->time_malloc_shared_pool = 0;
                        thread_data->time_malloc_total = 0;
                        thread_data->time_malloc_system_call = 0;
			thread_data->count_back2_shared_pool = 0;
                #endif

	}

	LOG_EPILOG();
        return pool;

}

int get_next_mlfq_to_assess(int mlfq_to_assess) {
	int next_mlfq = (mlfq_to_assess - 1);
	if (next_mlfq < 0) {
	    next_mlfq = MAX_MLFQ - 1;
	}
	return next_mlfq;
}

int compute_new_mlfq_idx(page_t * page) {
	LOG_PROLOG();
	// TODO: can try sampling technique as well
	// TODO: make claculation of index generic
	int avail_blocks = count_empty_blocks(page);
	uint32_t max_blocks = get_max_blocks(page);
	float percent_avail_blocks = (float)(avail_blocks)/max_blocks;
	if (percent_avail_blocks < 0.25) {
	    return 0;
	} else if (percent_avail_blocks < 0.5) {
	    return 1;
	} else if (percent_avail_blocks < 0.75) {
	    return 2;
	} else {
	    return 3;
	}
	LOG_EPILOG();
}

void assess_page(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, int bin_idx) {
	LOG_PROLOG();

	local_thread_data_t* thread_data = (pool->thread_data + thread_id);	
	
	int mlfq_to_assess = thread_data->mlfq_to_assess;

	assert(mlfq_to_assess >=0 && mlfq_to_assess < MAX_MLFQ);

	int count_list_checked = 0;

 	while(true) {
		// if the list is empty search the next one
		if (list_empty(&thread_data->bins[bin_idx][mlfq_to_assess])) {
			mlfq_to_assess = get_next_mlfq_to_assess(mlfq_to_assess);	
			count_list_checked++;
			// if all the MLFQ are empty 
			if (count_list_checked == MAX_MLFQ) {
			     break;
			}
			continue;
		}
		page_t *page = remove_from_tail(&thread_data->bins[bin_idx][mlfq_to_assess], &thread_data->tail[bin_idx][mlfq_to_assess]);
		// compute the new mlfq
		int new_mlfq_idx = compute_new_mlfq_idx(page);

		// if local pool has enough pages and this page has enoug blocks, move it to shared pool
		if ((new_mlfq_idx == MAX_MLFQ - 1) && (thread_data->total_pages[bin_idx] > pool->min_pages_per_bin)) {
			//int shared_pool_idx = randomNumber(0, pool->count_processors - 1);
			//LOG_INFO("random Number = %d", shared_pool_idx);
			add_page_shared_pool(shared_pool, page, thread_id, randomNumber(0, pool->count_processors - 1));
			#ifdef LOG_LEVEL_STATS
                        thread_data->count_back2_shared_pool++;
			#endif
			thread_data->total_pages[bin_idx]--;
			mlfq_to_assess = get_next_mlfq_to_assess(mlfq_to_assess);
			break;
		} else {
			// insert in the correct mlfq at last(head)
			add_at_head(&thread_data->bins[bin_idx][new_mlfq_idx], &thread_data->tail[bin_idx][new_mlfq_idx], page);
			mlfq_to_assess = get_next_mlfq_to_assess(mlfq_to_assess);
			break;
		}		
	}
	
	// set it for next time
	thread_data->mlfq_to_assess = mlfq_to_assess;
	LOG_EPILOG();
}

void* malloc_block_from_pool(local_pool_t *pool, shared_pool_t *shared_pool, int thread_id, int block_size) {
	LOG_PROLOG();

#ifdef LOG_LEVEL_STATS
        struct timeval s, e;
        gettimeofday(&s, NULL);
#endif	
	void* block = NULL;
        int bin_idx = 0;
        if (block_size > MIN_BLOCK_SIZE) {
                bin_idx = quick_log2(upper_power_of_two(block_size)) - quick_log2(MIN_BLOCK_SIZE);
        }
	
	assess_page(pool, shared_pool, thread_id, bin_idx);
        local_thread_data_t* thread_data = (pool->thread_data + thread_id);

#ifdef LOG_LEVEL_STATS
	        thread_data->count_malloc++;
#endif

	int mlfq_idx = MAX_MLFQ - 1;
	while(true) {
	    // get a page in the cache from the top 3 MLFQ
	    while (thread_data->page_cache[bin_idx] == NULL && mlfq_idx > 0) {
		    thread_data->page_cache[bin_idx] = remove_from_tail(&thread_data->bins[bin_idx][mlfq_idx], &thread_data->tail[bin_idx][mlfq_idx]);
		    mlfq_idx--;
	    }
	    // no pages in 3,2,1 MLFQ; get a page from shared pool
	    // update the count of pages in shared pool
	    if (thread_data->page_cache[bin_idx] == NULL) {

		    #ifdef LOG_LEVEL_STATS
        	    struct timeval ss, es;
        	    gettimeofday(&ss, NULL);
		    #endif
		    
		    //int shared_pool_idx = randomNumber(0, pool->count_processors - 1);
		    //LOG_INFO("random Number = %d", shared_pool_idx);
		    
		    thread_data->page_cache[bin_idx] = get_page_shared_pool(shared_pool, pool, thread_id, randomNumber(0, pool->count_processors - 1), block_size);
		    
		#ifdef LOG_LEVEL_STATS
                thread_data->count_request_shared_pool++;
                gettimeofday(&es, NULL);
                long int timeTakenSharedMalloc = ((es.tv_sec * 1000000 + es.tv_usec) - (ss.tv_sec * 1000000 + ss.tv_usec ));
                thread_data->time_malloc_shared_pool += timeTakenSharedMalloc;
		#endif

		    thread_data->total_pages[bin_idx]++;
		    //LOG_INFO("thread %d total_pages_local_pool = %d", thread_id, thread_data->total_pages[bin_idx]);
	    }

	    assert(thread_data->page_cache[bin_idx]);
	    block = malloc_block(thread_data->page_cache[bin_idx]);
	    // block would be null if the page in cache is out of pages.
	    if (block != NULL) {
	        break;
	    } else {
	         // save the page in MLFQ 0 grom cache
		 // and set cache to null
		 add_at_head(&thread_data->bins[bin_idx][0], &thread_data->tail[bin_idx][0], thread_data->page_cache[bin_idx]);
		 thread_data->page_cache[bin_idx] = NULL;
	    }
	}
	// block can not be null since page came either out of 3,2,1 mlfq or shared pool
	assert(block);

	#ifdef LOG_LEVEL_STATS
        gettimeofday(&e, NULL);
        long int timeTakenMalloc = ((e.tv_sec * 1000000 + e.tv_usec) - (s.tv_sec * 1000000 + s.tv_usec ));
        thread_data->time_malloc_total += timeTakenMalloc;
	#endif
	
	LOG_EPILOG();	
	return block;
}


void local_pool_stats(local_pool_t *pool) {
        LOG_PROLOG();

        LOG_DEBUG("########--LOCAL POOL--########");
        LOG_DEBUG("Number of threads = %d", pool->count_threads);
        LOG_INFO("Number of processors = %d", pool->count_processors);

        int thread = 0, bin = 0, mlfq = 0;
        local_thread_data_t* thread_data = NULL;
        int count_pages = 0;
        list_t* tmp = NULL;
        for (thread = 0; thread < pool->count_threads; ++thread) {
                LOG_DEBUG("\tThread %d", thread);
                //LOG_DEBUG("\t\tLast used shared pool idx %d", ((pool->thread_data + thread)->last_shared_pool_idx));
                thread_data = (pool->thread_data + thread);
                for (bin = 0; bin < MAX_BINS; ++bin) {
                        LOG_INFO("\t\tBin %d", bin);
                        for (mlfq = 0; mlfq < MAX_MLFQ; ++mlfq) {
                                count_pages = 0;
                                LOG_DEBUG("\t\t\tMLFQ %d", mlfq);
                                list_for_each(tmp, &(thread_data->bins[bin][mlfq])) {

                                        LOG_DEBUG("\t\t\t\tPage %d - Blocks = %d", count_pages, count_empty_blocks((page_t*) list_entry(tmp, page_header_t, node)));
                                        count_pages++;
                                }

                                LOG_DEBUG("\t\t\tMLFQ %d has %d pages", mlfq, count_pages);
                        }
                }
        }


#ifdef LOG_LEVEL_STATS
        LOG_STATS("########--LOCAL POOL STATS--########");
        long int total_mallocs = 0;
        long int time_malloc_total = 0;
        long int time_malloc_system_call = 0;
        long int time_malloc_shared_pool = 0;
        for (thread = 0; thread < pool->count_threads; ++thread) {
                LOG_STATS("\tThread %d", thread);
                thread_data = (pool->thread_data + thread);
                /*
		LOG_STATS("\t\tCount Malloc Ops = %ld", thread_data->count_malloc);
                LOG_STATS("\t\tCount MLFQ changes = %ld (%f %)", thread_data->count_mlfq_change, (float)(thread_data->count_mlfq_change * 100) / thread_data->count_malloc);
                LOG_STATS("\t\tCount Block counting ops = %ld", thread_data->count_blocks_counting_ops);
                */
		LOG_STATS("\t\tCount Shared pool requests = %ld (%f %)", thread_data->count_request_shared_pool, (float)(thread_data->count_request_shared_pool * 100) / thread_data->count_malloc);
                LOG_STATS("\t\tCount Back 2 Shared pool = %ld", thread_data->count_back2_shared_pool);
               
		 LOG_STATS("\t\tTotal time spent on malloc = %ld", thread_data->time_malloc_total);
                LOG_STATS("\t\tTotal time spent on malloc system call = %ld", thread_data->time_malloc_system_call);
                LOG_STATS("\t\tTotal time spent in shared pool = %ld (%f %)", thread_data->time_malloc_shared_pool, (float)(thread_data->time_malloc_shared_pool * 100) / thread_data->time_malloc_total);
                total_mallocs += thread_data->count_malloc;
                time_malloc_total += thread_data->time_malloc_total;
                time_malloc_system_call += thread_data->time_malloc_system_call;
                time_malloc_shared_pool += thread_data->time_malloc_shared_pool;
        }
        LOG_STATS("\t Malloc Ops = %ld", total_mallocs);
        LOG_STATS("\t Total time malloc = %ld", time_malloc_total);
        LOG_STATS("\t Total time malloc system call = %ld (%f %)", time_malloc_system_call, (float)(time_malloc_system_call * 100) / time_malloc_total);
        LOG_STATS("\t Malloc time in shared pool = %ld (%f %)", time_malloc_shared_pool, (float)(time_malloc_shared_pool * 100) / time_malloc_total);
#endif

        LOG_EPILOG();
}
               
