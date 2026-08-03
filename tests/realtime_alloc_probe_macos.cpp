#if !defined(__APPLE__)
#error "The realtime allocation interposition probe is macOS-only"
#endif

#include "realtime_alloc_probe_api.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <malloc/malloc.h>

namespace {

struct ThreadState {
    s3g_rt_alloc_probe_counts counts {};
    bool armed = false;
    bool insideHook = false;
};

// Constant-initialized POD TLS avoids heap work when the probe is first used
// on an audio thread. Counters intentionally belong only to the calling thread:
// allocations on UI or host worker threads must not contaminate the result.
thread_local ThreadState gState {};

void saturatingIncrement(uint64_t& value) noexcept
{
    if (value != std::numeric_limits<uint64_t>::max()) ++value;
}

void saturatingAdd(uint64_t& value, uint64_t increment) noexcept
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    value = increment > maximum - value ? maximum : value + increment;
}

uint64_t saturatingProduct(size_t first, size_t second) noexcept
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (first != 0u && second > maximum / first) return maximum;
    return static_cast<uint64_t>(first) * static_cast<uint64_t>(second);
}

bool validAlignment(size_t alignment) noexcept
{
    return alignment >= sizeof(void*)
        && (alignment % sizeof(void*)) == 0u
        && (alignment & (alignment - 1u)) == 0u;
}

class HookScope {
public:
    HookScope() noexcept
    {
        if (gState.armed && !gState.insideHook) {
            gState.insideHook = true;
            shouldCount_ = true;
        }
    }

    ~HookScope()
    {
        if (shouldCount_) gState.insideHook = false;
    }

    bool shouldCount() const noexcept { return shouldCount_; }

private:
    bool shouldCount_ = false;
};

malloc_zone_t* zoneForPointer(void* pointer) noexcept
{
    if (pointer != nullptr) {
        if (malloc_zone_t* zone = malloc_zone_from_ptr(pointer)) return zone;
    }
    return malloc_default_zone();
}

void recordFailure(bool counted, const void* result) noexcept
{
    if (counted && result == nullptr)
        saturatingIncrement(gState.counts.allocation_failures);
}

void* probeMalloc(size_t size) noexcept
{
    HookScope scope;
    if (scope.shouldCount()) {
        saturatingIncrement(gState.counts.malloc_calls);
        saturatingAdd(gState.counts.malloc_requested_bytes,
            static_cast<uint64_t>(size));
    }

    void* result = malloc_zone_malloc(malloc_default_zone(), size);
    recordFailure(scope.shouldCount(), result);
    if (result == nullptr) errno = ENOMEM;
    return result;
}

void* probeCalloc(size_t count, size_t size) noexcept
{
    HookScope scope;
    if (scope.shouldCount()) {
        saturatingIncrement(gState.counts.calloc_calls);
        saturatingAdd(gState.counts.calloc_requested_bytes,
            saturatingProduct(count, size));
    }

    void* result = malloc_zone_calloc(malloc_default_zone(), count, size);
    recordFailure(scope.shouldCount(), result);
    if (result == nullptr) errno = ENOMEM;
    return result;
}

void* probeRealloc(void* pointer, size_t size) noexcept
{
    HookScope scope;
    if (scope.shouldCount()) {
        saturatingIncrement(gState.counts.realloc_calls);
        saturatingAdd(gState.counts.realloc_requested_bytes,
            static_cast<uint64_t>(size));
    }

    void* result = malloc_zone_realloc(zoneForPointer(pointer), pointer, size);
    // realloc(ptr, 0) may legitimately return null after releasing ptr.
    if (size != 0u) {
        recordFailure(scope.shouldCount(), result);
        if (result == nullptr) errno = ENOMEM;
    }
    return result;
}

void probeFree(void* pointer) noexcept
{
    // C and C++ explicitly define free(nullptr) as a no-op. Do not report it
    // as realtime deallocation activity.
    if (pointer == nullptr) return;
    HookScope scope;
    if (scope.shouldCount()) saturatingIncrement(gState.counts.free_calls);

    const int savedErrno = errno;
    malloc_zone_free(zoneForPointer(pointer), pointer);
    errno = savedErrno;
}

int probePosixMemalign(void** resultPointer, size_t alignment,
    size_t size) noexcept
{
    HookScope scope;
    if (scope.shouldCount()) {
        saturatingIncrement(gState.counts.posix_memalign_calls);
        saturatingAdd(gState.counts.posix_memalign_requested_bytes,
            static_cast<uint64_t>(size));
    }

    const int savedErrno = errno;
    if (resultPointer == nullptr || !validAlignment(alignment)) {
        if (scope.shouldCount()) {
            saturatingIncrement(gState.counts.allocation_failures);
            saturatingIncrement(gState.counts.invalid_alignment_calls);
        }
        errno = savedErrno;
        return EINVAL;
    }

    void* result = malloc_zone_memalign(malloc_default_zone(), alignment, size);
    if (result == nullptr) {
        recordFailure(scope.shouldCount(), result);
        errno = savedErrno;
        return ENOMEM;
    }

    *resultPointer = result;
    errno = savedErrno;
    return 0;
}

void* probeAlignedAlloc(size_t alignment, size_t size) noexcept
{
    HookScope scope;
    if (scope.shouldCount()) {
        saturatingIncrement(gState.counts.aligned_alloc_calls);
        saturatingAdd(gState.counts.aligned_alloc_requested_bytes,
            static_cast<uint64_t>(size));
    }

    if (!validAlignment(alignment) || (size % alignment) != 0u) {
        if (scope.shouldCount()) {
            saturatingIncrement(gState.counts.allocation_failures);
            saturatingIncrement(gState.counts.invalid_alignment_calls);
        }
        errno = EINVAL;
        return nullptr;
    }

    void* result = malloc_zone_memalign(malloc_default_zone(), alignment, size);
    recordFailure(scope.shouldCount(), result);
    if (result == nullptr) errno = ENOMEM;
    return result;
}

#define S3G_DYLD_INTERPOSE(replacement, replacee)                              \
    __attribute__((used)) static struct {                                     \
        const void* replacementAddress;                                       \
        const void* replaceeAddress;                                          \
    } s3gInterpose_##replacee __attribute__((section("__DATA,__interpose"))) = { \
        reinterpret_cast<const void*>(                                        \
            reinterpret_cast<uintptr_t>(&replacement)),                       \
        reinterpret_cast<const void*>(                                        \
            reinterpret_cast<uintptr_t>(&replacee))                            \
    }

S3G_DYLD_INTERPOSE(probeMalloc, malloc);
S3G_DYLD_INTERPOSE(probeCalloc, calloc);
S3G_DYLD_INTERPOSE(probeRealloc, realloc);
S3G_DYLD_INTERPOSE(probeFree, free);
S3G_DYLD_INTERPOSE(probePosixMemalign, posix_memalign);
S3G_DYLD_INTERPOSE(probeAlignedAlloc, aligned_alloc);

#undef S3G_DYLD_INTERPOSE

} // namespace

#define S3G_PROBE_EXPORT extern "C" __attribute__((visibility("default")))

S3G_PROBE_EXPORT uint32_t s3g_rt_alloc_probe_abi_version(void)
{
    return S3G_RT_ALLOC_PROBE_ABI_VERSION;
}

S3G_PROBE_EXPORT void s3g_rt_alloc_probe_begin(void)
{
    gState.counts = {};
    gState.counts.abi_version = S3G_RT_ALLOC_PROBE_ABI_VERSION;
    gState.counts.struct_size = sizeof(s3g_rt_alloc_probe_counts);
    gState.insideHook = false;
    gState.armed = true;
}

S3G_PROBE_EXPORT void s3g_rt_alloc_probe_end(void)
{
    gState.armed = false;
}

S3G_PROBE_EXPORT int s3g_rt_alloc_probe_read(
    s3g_rt_alloc_probe_counts* outCounts, size_t outSize)
{
    if (outCounts == nullptr || outSize < sizeof(s3g_rt_alloc_probe_counts))
        return 0;
    *outCounts = gState.counts;
    return 1;
}

#undef S3G_PROBE_EXPORT
