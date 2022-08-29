#include "util.h"
#include <limits.h>

#ifndef __has_builtin
#define __has_builtin(_) 0
#endif

unsigned long log2lu(unsigned long n) {
#if __has_builtin(__builtin_clz)
    return sizeof(unsigned long) * CHAR_BIT - __builtin_clz(n) - 1;
#else
    unsigned long res = 0;
    while(n >>= 1) ++res;
    return res;
#endif
}

uint32_t rotl(uint32_t n, unsigned int c) {
    const unsigned int mask = (CHAR_BIT*sizeof(n) - 1);
    c &= mask;
    return (n<<c) | (n>>( (-c)&mask ));
}
