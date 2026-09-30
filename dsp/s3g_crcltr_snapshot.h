#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace s3g {
// Optional plug-in-only mirror. The DSP remains the sole writer; readers never
// touch its mutable playback buffers. Allocated when processing is suspended.
// A seqlock over atomic PCM gives a coherent, non-blocking snapshot or failure,
// never a mixture of two takes. Unchanged playback does not change the version.
class CrcltrSnapshot {
public:
    void prepare(uint32_t capacity) {
        left_=std::make_unique<std::atomic<float>[]>(capacity);
        right_=std::make_unique<std::atomic<float>[]>(capacity);
        frames_.store(0);version_.fetch_add(2);
    }
    void begin() noexcept {
        version_.fetch_add(1,std::memory_order_acq_rel);
        // Pair with the reader's acquire fence if it observes any new PCM,
        // even before the writer has published its final even version.
        std::atomic_thread_fence(std::memory_order_release);
    }
    void write(uint32_t frame,float left,float right) noexcept {
        left_[frame].store(left,std::memory_order_relaxed);
        right_[frame].store(right,std::memory_order_relaxed);
    }
    void end(uint32_t frames) noexcept {
        frames_.store(frames,std::memory_order_relaxed);
        version_.fetch_add(1,std::memory_order_release);
    }
    uint64_t version() const noexcept {return version_.load(std::memory_order_acquire);}
    uint32_t frames() const noexcept {return frames_.load(std::memory_order_relaxed);}
    bool copy(uint64_t version,float* left,float* right,uint32_t count) const noexcept {
        if((version&1)||this->version()!=version||count!=frames())return false;
        for(uint32_t i=0;i<count;++i){left[i]=left_[i].load(std::memory_order_relaxed);right[i]=right_[i].load(std::memory_order_relaxed);}
        std::atomic_thread_fence(std::memory_order_acquire);
        return this->version()==version;
    }
private:
    static_assert(std::atomic<float>::is_always_lock_free,"Audio snapshots require lock-free floats");
    std::unique_ptr<std::atomic<float>[]> left_,right_;
    std::atomic<uint64_t> version_{0};
    std::atomic<uint32_t> frames_{0};
};
}
