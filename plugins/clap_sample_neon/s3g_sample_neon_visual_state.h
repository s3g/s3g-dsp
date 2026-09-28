#pragma once
#include "s3g_sample_neon.h"
#include "s3g_sample_neon_visual.h"
#include <atomic>

namespace s3g::sample {
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<double>::is_always_lock_free
    && std::atomic<const SampleAsset*>::is_always_lock_free, "Neon playback telemetry must remain lock-free");
struct NeonVisualSnapshot {
    std::array<VoiceCursor, kMaximumVoices> cursors {};
    SampleNeonEngine::MotionVisual motion {};
    unsigned count = 0;
};

// Single audio-thread publisher; atomic payload avoids C++ seqlock data races.
// GUI makes at most two attempts and retains its last coherent frame if busy.
// No mutex, allocation, shared ownership or GUI work on the audio callback.
class NeonVisualPublication {
    struct Cursor {
        std::atomic<const SampleAsset*> asset {nullptr};
        std::array<std::atomic<float>, 10> values {};
        std::atomic<uint32_t> flags {0};
    };
    std::array<Cursor, kMaximumVoices> cursors_ {};
    std::atomic<unsigned> sequence_ {0}, count_ {0};
    std::atomic<double> seconds_ {0}, cycle_ {0};
    std::atomic<float> articulation_ {0};
    std::atomic<bool> active_ {false};
public:
    void publish(const std::array<VoiceCursor,kMaximumVoices>& source, unsigned count,
        SampleNeonEngine::MotionVisual motion = {}) noexcept {
        sequence_.fetch_add(1, std::memory_order_acq_rel);
        count = std::min<unsigned>(count, kMaximumVoices);
        for (unsigned n=0;n<count;++n) {
            const auto& c = source[n]; auto& out = cursors_[n];
            const float values[] {c.sourcePositionNormalized,c.sourceStartNormalized,c.sourceEndNormalized,
                c.windowPhase,c.level,c.windowSkew,c.attack,c.decay,c.sustain,c.release};
            out.asset.store(c.sourceAsset,std::memory_order_relaxed);
            for (unsigned i=0;i<10;++i) out.values[i].store(values[i],std::memory_order_relaxed);
            out.flags.store(uint32_t(c.window) | (uint32_t(c.layer)<<8u) | (uint32_t(c.reverse)<<16u),std::memory_order_relaxed);
        }
        seconds_.store(motion.seconds,std::memory_order_relaxed); cycle_.store(motion.cycle,std::memory_order_relaxed);
        articulation_.store(motion.articulation,std::memory_order_relaxed); active_.store(motion.active,std::memory_order_relaxed);
        count_.store(count,std::memory_order_relaxed);
        sequence_.fetch_add(1,std::memory_order_release);
    }
    void clear() noexcept { static const std::array<VoiceCursor,kMaximumVoices> empty {}; publish(empty,0); }
    bool read(NeonVisualSnapshot& result) const noexcept {
        for (unsigned attempt=0;attempt<2;++attempt) {
            const auto before = sequence_.load(std::memory_order_acquire);
            if (before & 1u) continue;
            NeonVisualSnapshot next;
            next.count = std::min<unsigned>(count_.load(std::memory_order_relaxed),kMaximumVoices);
            for (unsigned n=0;n<next.count;++n) {
                const auto& in = cursors_[n]; auto& c = next.cursors[n];
                c.sourceAsset = in.asset.load(std::memory_order_relaxed);
                float* values[] {&c.sourcePositionNormalized,&c.sourceStartNormalized,&c.sourceEndNormalized,
                    &c.windowPhase,&c.level,&c.windowSkew,&c.attack,&c.decay,&c.sustain,&c.release};
                for (unsigned i=0;i<10;++i) *values[i] = in.values[i].load(std::memory_order_relaxed);
                const auto flags = in.flags.load(std::memory_order_relaxed);
                c.window=static_cast<uint8_t>(flags); c.layer=static_cast<uint8_t>(flags>>8u); c.reverse=(flags & 65536u)!=0;
            }
            next.motion = {seconds_.load(std::memory_order_relaxed),cycle_.load(std::memory_order_relaxed),
                articulation_.load(std::memory_order_relaxed),active_.load(std::memory_order_relaxed)};
            std::atomic_thread_fence(std::memory_order_acquire);
            if (before == sequence_.load(std::memory_order_relaxed)) { result=next; return true; }
        }
        return false;
    }
};
}
