#pragma once
#include <array>
#include <atomic>

namespace s3g::clap_gui {
// SPSC triple buffer: the audio writer and main-thread reader always own
// different slots; only the middle slot is exchanged. No locks, retries,
// allocations, or copies occur in the handoff itself.
template <class T> class CaptureSnapshot {
public:
  std::array<T, 3> data{};
  int begin() { return back; }
  void cancel(int) {}
  void publish(int n) {
    if (n >= 0)
      back = middle.exchange(n | dirty, std::memory_order_acq_rel) & mask;
  }
  template <class F> bool read(F &&consume) const {
    if (middle.load(std::memory_order_acquire) & dirty) {
      front = middle.exchange(front, std::memory_order_acq_rel) & mask;
      received = true;
    }
    if (!received)
      return false;
    consume(data[front]);
    return true;
  }

private:
  static constexpr int dirty = 4, mask = 3;
  int back = 2;
  mutable int front = 0;
  mutable bool received = false;
  mutable std::atomic<int> middle{1};
};
} // namespace s3g::clap_gui
