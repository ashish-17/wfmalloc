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

#define GET_TAGGED_PTR(ptr, type, tag) ((type*)((long)(ptr) | ((long)(tag) << (TOTAL_BITS-COUNT_TAG_BITS))))
#define GET_PTR_FROM_TAGGEDPTR(ptr, type) ((type*)((long)(ptr) & (long)(~((long)(~0) << (TOTAL_BITS-COUNT_TAG_BITS)))))
#define GET_TAG_FROM_TAGGEDPTR(ptr) ((unsigned int)(((long)(ptr) & (long)((long)(~0) << (TOTAL_BITS-COUNT_TAG_BITS))) >> (TOTAL_BITS-COUNT_TAG_BITS)))

#endif /* INCLUDES_UTILS_H_ */
