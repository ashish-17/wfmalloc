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
		case 8192:	return 12;
		case 16384:	return 13;
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

#endif /* INCLUDES_UTILS_H_ */
