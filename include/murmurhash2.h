#ifndef MURMURHASH2_H
#define MURMURHASH2_H

#include <stddef.h>
#include <stdint.h>

uint32_t murmurhash2(const void *key, size_t len, uint32_t seed);

#endif
