// Deliberate live INPUT-ONLY check for two connected USB NEONs. No output port,
// MIDI sends, device property writes or host/project changes. Not a CTest test.
// clang++ -std=c++17 -pthread -I dsp -I plugins/common -framework CoreMIDI \
//   -framework CoreFoundation scripts/neon-usb-input-check.cpp -o /tmp/neon-usb-input-check
#include "s3g_neon_usb_input.h"
#include <chrono>
#include <cstdio>
#include <thread>

using s3g::controller::neon_midi::UsbInput;
template<class Predicate> bool waitFor(Predicate predicate) {
    for (unsigned n=0;n<200;++n) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
int main() {
    UsbInput first, competing;
    first.wanted.store(2); first.start();
    if (!waitFor([&] { return first.connected[0].load() && first.connected[1].load(); })) {
        std::fprintf(stderr,"FAIL: connect two NEONs directly by USB; direct MIDI must not be disabled.\n"); return 1;
    }
    const auto a = first.source[0].load(), b = first.source[1].load();
    const auto da = first.destination[0].load(), db = first.destination[1].load();
    std::printf("U1 input=%d output=%d; U2 input=%d output=%d\n", a,da,b,db);
    if (!a || !b || a==b || !da || !db || da==db) return 2;
    first.assignmentRequest.store(1);
    if (!waitFor([&] { return first.source[0].load()==b && first.source[1].load()==a
        && first.destination[0].load()==db && first.destination[1].load()==da
        && first.connected[0].load() && first.connected[1].load(); })) return 3;
    competing.wanted.store(2); competing.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (competing.connected[0].load() || competing.connected[1].load()) return 4;
    first.wanted.store(1);
    if (!waitFor([&] { return first.connected[0].load() && !first.connected[1].load(); })) return 5;
    if (first.source[0].load()!=b || first.destination[0].load()!=db) return 6;
    first.stop();
    if (!waitFor([&] { return competing.connected[0].load() && competing.connected[1].load(); })) return 7;
    competing.stop();
    if (first.errors.load() || competing.errors.load()) return 8;
    std::puts("PASS: paired destinations, logical swap, per-process ownership, one-unit mode, release/reacquire. No MIDI sent.");
    return 0;
}
