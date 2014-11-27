/*
 * misc useful #defines ...
 */
#include <stddef.h>

#define container_of(ptr, type, member) ({			\
        const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define array_size(x) (sizeof(x)/sizeof(x[0]))
