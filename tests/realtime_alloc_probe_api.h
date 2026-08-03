#pragma once

#include <stddef.h>
#include <stdint.h>

// C ABI exported by the opt-in macOS realtime allocation probe. The probe is
// normally loaded with DYLD_INSERT_LIBRARIES, then discovered with
// dlsym(RTLD_DEFAULT, ...). It remains inert until begin() is called on the
// thread that is about to run the realtime callback.

#define S3G_RT_ALLOC_PROBE_ABI_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct s3g_rt_alloc_probe_counts {
    uint32_t abi_version;
    uint32_t struct_size;

    uint64_t malloc_calls;
    uint64_t malloc_requested_bytes;
    uint64_t calloc_calls;
    uint64_t calloc_requested_bytes;
    uint64_t realloc_calls;
    uint64_t realloc_requested_bytes;
    uint64_t free_calls;
    uint64_t posix_memalign_calls;
    uint64_t posix_memalign_requested_bytes;
    uint64_t aligned_alloc_calls;
    uint64_t aligned_alloc_requested_bytes;

    uint64_t allocation_failures;
    uint64_t invalid_alignment_calls;
} s3g_rt_alloc_probe_counts;

// Returns S3G_RT_ALLOC_PROBE_ABI_VERSION. This is useful for rejecting an
// incompatible injected dylib before entering the measured callback loop.
uint32_t s3g_rt_alloc_probe_abi_version(void);

// Clears the calling thread's counters and arms that thread only.
void s3g_rt_alloc_probe_begin(void);

// Disarms the calling thread. Counters remain available to read.
void s3g_rt_alloc_probe_end(void);

// Copies the calling thread's current counters without allocating. Returns 1
// on success, or 0 when out_counts is null or out_size is too small.
int s3g_rt_alloc_probe_read(
    s3g_rt_alloc_probe_counts* out_counts, size_t out_size);

#ifdef __cplusplus
}
#endif

