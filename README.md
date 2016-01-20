# wfmalloc
A wait-free implementation of a concurrent data object is one that guarantees that any process can complete any operation in a finite number of’ steps, regardless of the execution speeds of the other processes. Here concurrent data object is the data structure shared by concurrent processes.

In this project we plan to create a wait-free dynamic memory allocator.

##General Design
Other allocators generally have two tier design structure. At the lowest level all the threads have their own
local pools which are contention free and at the second level the threads share the global pool. Since the
local pools can only be used by a single thread, different allocators differ in the way they design their global
pools. To handle requests of multiple sized blocks, an allocator would maintain bins of different sizes. A
block request is rounded up to the nearest bin size and then the request is serviced from that bin. Bins are
maintained upto certain threshold and the requests greater than that threshold are serviced directly from
the OS.

##Our design
We too have local pools for each thread and a global pool. Global pool is subdivided into smaller pools.
These smaller pools (we call them stacks) have similar structure as thread-local local pools. The number of
stacks in global pool is configurable and can not be greater than the maximum number, n, of threads that
can use the allocator simultaneously.

Local pool will have bins of different sizes. By different sized bins, we mean that these bins will be used to
service requests of different sizes. Each bin can contain one or more memory pages which will be broken into
blocks to service the memory requests. Stacks of global pool have the same structure. The only difference is
that they are not contention free (thus have a more complex structure - described later).
Allocator requests memory from the OS (for whatever reasons - a bin is out of pages or stealing of pages
is no longer possible) in multiples (generally 1) of pages. Blocks are later carved out of pages on thread
requests. We maintain a header at the beginning of each page which is a binary array of size equal to number
of blocks in that page. Each entry indicates whether the corresponding block is free or allocated. Pages in
bins are maintained as singly linked list.

When local pools are out of memory, threads get (almost) free pages from the global pool into local pools.
Due to this scheme, a lot of pages can get accumulated in the local pools of threads. Thus, a thread pushes
back a page from its local pool to global pool if the page is more than emptiness threshold (tunable) free.
On finding local pool empty, threads go to their stack in global pool for more pages. If they find their stack
empty as well, they try to steal pages from stacks of other threads. After making the steal attempt on the
stacks of all other threads, if they fail to steal, they ask for help. They do this by setting a flag in helping
array which is again a boolean array of size n. On noticing that a thread needs help, others first help the
thread in need before stealing for themselves (TODO: some design decisions are to be made).

##Malloc(int x)
1. y = round − up(x).
2. Go to your own local pool and check for block in size y bin.
3. If block found, push all the pages in that bin satisfying the emptiness threshold back to global pool
stack and return.
4. If not found, try in global pool stack for a page.
5. If page found, save it local pool and go to step 3.
6. If not found try stealing.
7. To steal, try dequeuing a page from the next thread’s stack.
8. If successful, save it in local pool and go to step 3.
9. If not successful for less than n − 1 attempts, go to step 7.
10. If not successful for n − 1 steps, raise flag in helping array.
11. TODO: Donation is needed. Simple helping is not enough.

##Free(ptr t)
1. Use block header to determine page header.
2. In page header reset the allocated bit for the corresponding block.

##Advantages
1. Locality of reference
2. Wait freedom
3. Easy to use in Pd-Cr scenario.
4. No passive false sharing: Consumers return the memory back to producers.
5. Active false sharing: ?? TODO
6. Malloc is O(TODO) and free is O(1).

##Disadvantages
1. Can not handle dynamic number of threads.
2. Malloc is expensive.
3. External Fragmentation: Since a page can only service requests of one size, a lot of pages can be
allocated from the OS to cater requests of different sizes when in fact only one block of each size is
requested. This leads to very high ratio of used memory to allocated from OS memory.

##Problems to be taken care of while coding
1. ABA Problem : Use counters to alleviate this.
2. Race condition that occurs due to helping. Either help in helping array (the way we discussed today)
or make sure that no one steals fro a stack if there is only one page in it.
3. Donation is needed.
4. Probably hazardpointers would be needed too.

## TODO:
1. Remove malloc from the code
2. Check for execution path > 512
3. Remove thread id from wfmalloc. Threadlocal can be used in some way.
4. What about destroying the allocator. There is init but no destruction.
5. Create static library.
6. Write the wrappers for wffree and wfmalloc.
7. Allocate larger number of pages from OS.
8. How are you maintaining the wfqueue for the memory that you ask from the OS
