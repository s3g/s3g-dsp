#include "realtime_alloc_probe_api.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <new>

namespace {

// Keep the optimized self-test from deleting matched allocation/free pairs.
// This test is macOS/Clang-specific by design, just like the injected dylib.
__attribute__((noinline)) void escapePointer(void* pointer)
{
    __asm__ volatile("" : : "r"(pointer) : "memory");
}

template <typename Function>
Function loadFunction(const char* name)
{
    dlerror();
    void* symbol = dlsym(RTLD_DEFAULT, name);
    if (const char* error = dlerror()) {
        std::cerr << "missing injected probe symbol " << name << ": "
                  << error << '\n';
        return nullptr;
    }
    return reinterpret_cast<Function>(symbol);
}

} // namespace

int main()
{
    using AbiVersion = uint32_t (*)();
    using Begin = void (*)();
    using End = void (*)();
    using Read = int (*)(s3g_rt_alloc_probe_counts*, size_t);

    const AbiVersion abiVersion =
        loadFunction<AbiVersion>("s3g_rt_alloc_probe_abi_version");
    const Begin begin = loadFunction<Begin>("s3g_rt_alloc_probe_begin");
    const End end = loadFunction<End>("s3g_rt_alloc_probe_end");
    const Read read = loadFunction<Read>("s3g_rt_alloc_probe_read");
    if (abiVersion == nullptr || begin == nullptr || end == nullptr
        || read == nullptr) {
        std::cerr << "run this test with the probe dylib in "
                     "DYLD_INSERT_LIBRARIES\n";
        return 1;
    }
    if (abiVersion() != S3G_RT_ALLOC_PROBE_ABI_VERSION) {
        std::cerr << "probe ABI mismatch\n";
        return 1;
    }

    begin();
    void* first = std::malloc(13u);
    escapePointer(first);
    void* second = std::calloc(3u, 7u);
    escapePointer(second);
    first = std::realloc(first, 29u);
    escapePointer(first);
    void* third = nullptr;
    const int alignedResult = posix_memalign(&third, 64u, 32u);
    escapePointer(third);
    void* fourth = aligned_alloc(64u, 64u);
    escapePointer(fourth);
    auto* fifth = new (std::nothrow) unsigned char[17u];
    escapePointer(fifth);
    const bool allocationsSucceeded = first != nullptr && second != nullptr
        && third != nullptr && fourth != nullptr && fifth != nullptr;
    // Keep this indirect so an optimized build cannot fold away the
    // standards-defined no-op before the interposition probe observes it.
    using FreeFunction = void (*)(void*);
    FreeFunction volatile freeFunction = &std::free;
    freeFunction(nullptr);
    std::free(first);
    std::free(second);
    std::free(third);
    std::free(fourth);
    delete[] fifth;
    end();

    s3g_rt_alloc_probe_counts counts {};
    if (read(&counts, sizeof(counts)) != 1) {
        std::cerr << "could not read probe counters\n";
        return 1;
    }

    const bool callsMatch = counts.malloc_calls == 2u
        && counts.calloc_calls == 1u
        && counts.realloc_calls == 1u
        && counts.free_calls == 5u
        && counts.posix_memalign_calls == 1u
        && counts.aligned_alloc_calls == 1u;
    const bool bytesMatch = counts.malloc_requested_bytes == 30u
        && counts.calloc_requested_bytes == 21u
        && counts.realloc_requested_bytes == 29u
        && counts.posix_memalign_requested_bytes == 32u
        && counts.aligned_alloc_requested_bytes == 64u;
    if (!callsMatch || !bytesMatch || alignedResult != 0
        || !allocationsSucceeded) {
        std::cerr << "unexpected probe counts: malloc=" << counts.malloc_calls
                  << " calloc=" << counts.calloc_calls
                  << " realloc=" << counts.realloc_calls
                  << " free=" << counts.free_calls
                  << " posix_memalign=" << counts.posix_memalign_calls
                  << " aligned_alloc=" << counts.aligned_alloc_calls << '\n';
        return 1;
    }

    // Work performed after end() must not change the retained measurement.
    void* unmeasured = std::malloc(9u);
    escapePointer(unmeasured);
    std::free(unmeasured);
    s3g_rt_alloc_probe_counts afterEnd {};
    if (read(&afterEnd, sizeof(afterEnd)) != 1
        || afterEnd.malloc_calls != counts.malloc_calls
        || afterEnd.free_calls != counts.free_calls) {
        std::cerr << "probe continued counting after end()\n";
        return 1;
    }

    std::cout << "realtime allocation probe self-test passed\n";
    return 0;
}
