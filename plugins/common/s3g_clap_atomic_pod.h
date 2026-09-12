#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace s3g::clap_gui {
// Single writer (the serialized process/flush/state-load owner), any readers.
// Atomic words avoid the data race of an ordinary-memory seqlock. Readers make
// bounded attempts and retain their previous GUI frame if a writer is busy.
template<class T> class AtomicPod {
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  static constexpr size_t count = (sizeof(T) + 7u) / 8u;
  std::array<std::atomic<uint64_t>, count> words{};
  std::atomic<uint64_t> sequence{0};
public:
  void store(const T& value) noexcept {
    std::array<uint64_t, count> copy{};
    std::memcpy(copy.data(), &value, sizeof(T));
    sequence.fetch_add(1, std::memory_order_acq_rel);
    for (size_t n = 0; n < count; ++n) words[n].store(copy[n], std::memory_order_release);
    sequence.fetch_add(1, std::memory_order_release);
  }
  bool load(T& value) const noexcept {
    std::array<uint64_t, count> copy{};
    for (int attempt = 0; attempt < 8; ++attempt) {
      const auto before = sequence.load(std::memory_order_acquire);
      if (before & 1u) continue;
      for (size_t n = 0; n < count; ++n) copy[n] = words[n].load(std::memory_order_acquire);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (before == sequence.load(std::memory_order_relaxed)) {
        std::memcpy(&value, copy.data(), sizeof(T)); return true;
      }
    }
    return false;
  }
};
}
