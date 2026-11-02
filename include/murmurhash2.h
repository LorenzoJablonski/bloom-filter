#ifndef MURMURHASH2_H
#define MURMURHASH2_H

#include <stddef.h>
#include <stdint.h>

uint32_t murmurhash2(const void *key, size_t len, uint32_t seed);

/*
 * Optional call counter for instrumentation. When non-NULL, every call to
 * murmurhash2() increments *murmurhash2_counter. Keep it NULL during timed
 * runs; enable it only in a dedicated, single-threaded counting pass.
 */
extern uint64_t *murmurhash2_counter;

#endif
