#include "../plugins/common/s3g_capture_snapshot.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
struct Payload {
  uint64_t serial = 0;
  std::array<uint64_t, 1024> values{};
};
int main() {
  s3g::clap_gui::CaptureSnapshot<Payload> snapshot;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (uint64_t serial = 1; serial <= 100000; ++serial) {
      const int slot = snapshot.begin();
      auto &payload = snapshot.data[slot];
      payload.serial = serial;
      payload.values.fill(serial);
      snapshot.publish(slot);
    }
    done.store(true, std::memory_order_release);
  });
  bool ok = true;
  uint64_t previous = 0;
  do {
    snapshot.read([&](const Payload &payload) {
      ok = ok && payload.serial >= previous;
      for (auto value : payload.values)
        ok = ok && value == payload.serial;
      previous = payload.serial;
    });
  } while (!done.load(std::memory_order_acquire));
  writer.join();
  snapshot.read(
      [&](const Payload &payload) { ok = ok && payload.serial == 100000; });
  if (!ok)
    return 1;
  std::cout << "Capture-state snapshot handoff stress passed\n";
}
