#include "hashmap.h"
#include <stdlib.h>

int seed = 0;

void init_seed() {
    if(seed) return;
    seed = random();
}

// All the code from here:
// Is taken from the internet because I needed a simple hash function
// --------------------------------------------------------

#define PRIME1  0x9E3779B1U  /*!< 0b10011110001101110111100110110001 */
#define PRIME2  0x85EBCA77U  /*!< 0b10000101111010111100101001110111 */
#define PRIME3  0xC2B2AE3DU  /*!< 0b11000010101100101010111000111101 */
#define PRIME4  0x27D4EB2FU  /*!< 0b00100111110101001110101100101111 */
#define PRIME5  0x165667B1U  /*!< 0b00010110010101100110011110110001 */

uint32_t _rotl(const uint32_t value, int shift) {
    if ((shift &= sizeof(value)*8 - 1) == 0)
      return value;
    return (value << shift) | (value >> (sizeof(value)*8 - shift));
}

uint32_t _rotr(const uint32_t value, int shift) {
    if ((shift &= sizeof(value)*8 - 1) == 0)
      return value;
    return (value >> shift) | (value << (sizeof(value)*8 - shift));
}

uint32_t hash(uint8_t * data, int len) {
    int end = len;
    int offset = 0;
    int h32;
    if (len >= 16) {
        int limit = end - 16;
        uint32_t v1 = seed + PRIME1 + PRIME2;
        uint32_t v2 = seed + PRIME2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - PRIME1;

        do {
            v1 += (*(uint32_t*)(data + offset)) * PRIME2;
            v1 = _rotl(v1, 13);
            v1 *= PRIME1;
            offset += 4;
            v2 += (*(uint32_t*)(data + offset)) * PRIME2;
            v2 = _rotl(v2, 13);
            v2 *= PRIME1;
            offset += 4;
            v3 += (*(uint32_t*)(data + offset)) * PRIME2;
            v3 = _rotl(v3, 13);
            v3 *= PRIME1;
            offset += 4;
            v4 += (*(uint32_t*)(data + offset)) * PRIME2;
            v4 = _rotl(v4, 13);
            v4 *= PRIME1;
            offset += 4;
        } while(offset <= limit);
        // main loop ends
        // mix
        h32 = _rotl(v1, 1) + _rotl(v2, 7) + _rotl(v3, 12) + _rotl(v4, 18);
    } else {
        h32 = seed + PRIME5;
    }

    for(h32 += len; offset <= end - 4; offset += 4) {
        h32 += (*(uint32_t*)(data + offset)) * PRIME3;
        h32 = _rotl(h32, 17) * PRIME4;
    }

    while(offset < end) {
        h32 += (data[offset] & 255) * PRIME5;
        h32 = _rotl(h32, 11) * PRIME1;
        ++offset;
    }

    h32 ^= h32 >> 15;
    h32 *= PRIME2;
    h32 ^= h32 >> 13;
    h32 *= PRIME3;
    h32 ^= h32 >> 16;
    return h32;
}

// --------------------------------------------------------
// To here


