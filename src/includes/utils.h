/*
 * utils.h
 *
 *  Created on: Dec 27, 2015
 *      Author: ashish
 */

#ifndef INCLUDES_UTILS_H_
#define INCLUDES_UTILS_H_


#define OFFSETOF(type, field)    ((unsigned long) &(((type *) 0)->field))

#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

static inline unsigned int quick_log2(unsigned int x)
{
	switch (x) {
		case 1:		return 0;
		case 2:		return 1;
		case 4:		return 2;
		case 8:		return 3;
		case 16:	return 4;
		case 32:	return 5;
		case 64:	return 6;
		case 128:	return 7;
		case 256:	return 8;
		case 512:	return 9;
		case 1024:	return 10;
		case 2048:	return 11;
		case 4096:	return 12;
		case 8192:	return 13;
		case 16384:	return 14;
	}

	return -1;
}

static inline unsigned int quick_pow2(unsigned int x)
{
	switch (x) {
		case 0:		return 1;
		case 1:		return 2;
		case 2:		return 4;
		case 3:		return 8;
		case 4:		return 16;
		case 5:		return 32;
		case 6:		return 64;
		case 7:		return 128;
		case 8:		return 256;
		case 9:		return 512;
		case 10:	return 1024;
		case 11:	return 2048;
		case 12:	return 4096;
		case 13:	return 8192;
		case 14:	return 16384;
	}

	return -1;
}

static inline unsigned long upper_power_of_two(unsigned long v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;

}


#define COUNT_TAG_BITS 16
#define TOTAL_BITS 64



#define TOTAL_BITS 64
#define COUNT_TAG_BITS 16
#define PTR_BITS (TOTAL_BITS - COUNT_TAG_BITS)

#define TAGGEDPTR_PTR_MASK (~(((uintptr_t)~0) << (uintptr_t)PTR_BITS))


#include<assert.h>
//#define assert(x) 0
#include<stdint.h>
//#include<stdio.h>

static void* get_ptr_from_taggedptr(void* tp);
static uint16_t get_tag_from_taggedptr(void* tp);

static void* get_tagged_ptr(void* ptr, uint16_t tag) {
    //if((uintptr_t)ptr & (~((uintptr_t)TAGGEDPTR_PTR_MASK))) {
    /*
    if((uintptr_t)ptr > (((uintptr_t)TAGGEDPTR_PTR_MASK))) {
        fprintf(stderr, "Bad Pointer.  %p\n", ptr);
    }
     */
    assert(((uintptr_t)ptr <= 0xfffffffffffful));
    void* r1 = (void*)((((long)(ptr)) | ((long)(tag) << (TOTAL_BITS-COUNT_TAG_BITS))));
    void* r2 = (void*)(((uintptr_t)ptr) | (((uintptr_t)tag) << (uintptr_t)(PTR_BITS)));
    assert(r1 == r2);
    assert(get_ptr_from_taggedptr(r1) == ptr);
    assert(get_tag_from_taggedptr(r1) == tag);
    return r1;
}

static void* get_ptr_from_taggedptr(void* tp) {
    void* r1 = (void*)((long)(tp) & (long)(~((long)(~0) << (TOTAL_BITS-COUNT_TAG_BITS))));
    void* r2 = (void*)((uintptr_t)tp & TAGGEDPTR_PTR_MASK);
    assert(r1 == r2);
    return r1;
}

static uint16_t get_tag_from_taggedptr(void* ptr) {
    uint16_t r1 = (((unsigned long)(ptr) & (unsigned long)((unsigned long)(~0) << (TOTAL_BITS-COUNT_TAG_BITS))) >> (TOTAL_BITS-COUNT_TAG_BITS));
    uint16_t r2 = (((uintptr_t)(ptr) >> (uintptr_t)PTR_BITS));
    assert(r1 == r2);
    return r1;
}

#if 0
#define GET_TAGGED_PTR(ptr, type, tag) ((type*)get_tagged_ptr(ptr, tag))
#define GET_PTR_FROM_TAGGEDPTR(tp, type) ((type*)get_ptr_from_taggedptr(tp))
#define GET_TAG_FROM_TAGGEDPTR(tp) get_tag_from_taggedptr(tp)

#else
#define GET_TAGGED_PTR(ptr, type, tag) ((type*)(((long)(ptr)) | ((long)(tag) << (TOTAL_BITS-COUNT_TAG_BITS))))
#define GET_PTR_FROM_TAGGEDPTR(ptr, type) ((type*)((long)(ptr) & (long)(~((long)(~0) << (TOTAL_BITS-COUNT_TAG_BITS)))))
#define GET_TAG_FROM_TAGGEDPTR(ptr) ((unsigned int)(((unsigned long)(ptr) & (unsigned long)((unsigned long)(~0) << (TOTAL_BITS-COUNT_TAG_BITS))) >> (TOTAL_BITS-COUNT_TAG_BITS)))
#endif

#endif /* INCLUDES_UTILS_H_ */
