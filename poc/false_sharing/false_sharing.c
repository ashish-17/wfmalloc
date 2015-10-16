#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>

const long long int  NUM_ITERS = 10000000;
const int NUM_THREADS = 4;
const int MIN_BLOCK_SIZE = 10;
const int BLOCK_SIZE_INCR = 54;

char* global_memory = NULL;

typedef struct {
	int index;
	int block_size;
	long long int num_iters;
} thread_data;


void test_code(int block_size, int num_threads, long long int num_iters);
void* worker(void* ptr);
size_t cache_line_size();

int main() {

	printf("\nSize of cache line = %zu", cache_line_size());	
	for (int i = 1; i <= 1; ++i) 
		test_code(MIN_BLOCK_SIZE + i*BLOCK_SIZE_INCR, NUM_THREADS, NUM_ITERS);
	
	
	for (int i = 20; i >= 30; --i) 
		test_code(MIN_BLOCK_SIZE + i*BLOCK_SIZE_INCR, NUM_THREADS, NUM_ITERS);
	
	return 0;
}

void test_code(int block_size, int num_threads, long long int num_iters) {
	
	global_memory = (char*)malloc(block_size*num_threads);
	
	struct timeval s, e;
	gettimeofday(&s, NULL);
         
	pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t)*num_threads);
	thread_data *data = (thread_data*)malloc(sizeof(thread_data)*num_threads);
	for (int i=0; i<num_threads; ++i) {
		//printf("\n Creating thread - %d", i);
		thread_data *tmp = data + i;
		tmp->index = i;
		tmp->block_size = block_size;
		tmp->num_iters = num_iters;
		pthread_create(threads+i, NULL, worker, (void*)(tmp));
	}

	for (int i = 0; i<num_threads; ++i) {
		pthread_join(threads[i], NULL);
	}

	gettimeofday(&e, NULL);

	long int timeTaken = ((e.tv_sec * 1000000 + e.tv_usec) - (s.tv_sec * 1000000 + s.tv_usec ))/1000;
                                                     
    printf("\n Time spent(%d) = %ld", block_size, timeTaken);
	
	free(data);
	free(threads);
	free(global_memory);
}

void* worker(void* ptr) {
	//printf("\nSome thread's worker...");
	thread_data *t = (thread_data*)ptr;
	for (long long int k = 0; k < t->num_iters; ++k) {
		for (int i = 0; i<MIN_BLOCK_SIZE; ++i) {
			char* tmp = (char*)global_memory + t->index*t->block_size + i;
			char x = *tmp;
			x=x+1;
			*tmp = 'a'+i;
		}
	}
}

size_t cache_line_size() {
    FILE * p = 0;
    p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
    unsigned int i = 0;
    if (p) {
        fscanf(p, "%d", &i);
        fclose(p);
    }
    return i;
}
