#include "s3g/tracker/preview_sequencer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

static thread_local bool inAudio = false;
static std::atomic<unsigned> audioAllocations {0}, audioDeletions {0};
void* operator new(std::size_t n) {
    if (inAudio) ++audioAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (inAudio && p) ++audioDeletions;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

using namespace s3g::tracker;
int main() {
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { ++failures; std::cerr << message << '\n'; }
    };
    double worstError = 0;
    // No UI ticks at all during 128 complete loops. The first hit is row 2,
    // leaving both leading and trailing rests in the 16-row musical period.
    for (double rate : {44100., 48000., 96000.})
    for (double bpm : {90., 97.5, 120., 123., 130., 145.5})
    for (uint32_t block : {64u, 127u, 512u, 8192u}) {
        PreviewSequencer player;
        const auto token = player.publish({{2, 60, 100, 70},
            {10, 62, 90, 100, 32768}}, 10, 120, 4, 16, true, true);
        uint64_t frame = 0, hitIndex = 0;
        const double rowFrames = rate * 60 / (bpm * 4);
        const auto end = uint64_t(std::ceil(rowFrames * 16 * 128));
        while (frame < end) {
            const auto count = uint32_t(std::min<uint64_t>(block, end - frame));
            inAudio = true;
            player.beginBlock(count, rate, bpm);
            PreviewHit hit;
            while (player.next(hit)) {
                const double row = double(hitIndex / 2) * 16 +
                    (hitIndex % 2 ? 10.5 : 2);
                const double error = double(frame + hit.offset) - row * rowFrames;
                worstError = std::max(worstError, std::abs(error));
                // Do not round the row length and then multiply at each wrap.
                check(error > -1e-5 && error < 1.00001,
                    "onset drifted by more than one sample");
                check(hit.channel == 10 && hit.event.note == (hitIndex % 2 ? 62 : 60),
                    "loop routing/event order changed");
                ++hitIndex;
            }
            player.endBlock();
            inAudio = false;
            frame += count;
        }
        check(hitIndex == 256, "missing/duplicated loop hits");
        check(player.position(token) >= 0, "loop stopped without UI ticks");
        player.cancel(token);
        inAudio = true;
        player.beginBlock(512, rate, bpm);
        PreviewHit hit;
        check(!player.next(hit), "cancel left future loop notes scheduled");
        player.endBlock();
        inAudio = false;
        check(player.position(token) == -1, "stop did not clear cursor");
    }
    {
        PreviewSequencer p;
        const auto token = p.publish({{0, 60, 100, 100}}, 1, 120, 4, 4, true, true);
        auto run = [&](uint32_t frames, double bpm, double increment = 0.) {
            std::vector<uint32_t> hits;
            p.beginBlock(frames, 48000, bpm, increment);
            PreviewHit h;
            while (p.next(h)) hits.push_back(h.offset);
            p.endBlock();
            return hits;
        };
        check(run(12000, 120) == std::vector<uint32_t>{0}, "first half-loop");
        // 2 rows at 120 BPM, then 2 rows at 240 BPM: seam at frame 18000.
        check(run(6001, 240) == std::vector<uint32_t>{6000}, "tempo change lost phase");
        check(p.position(token) == 0, "cursor does not follow changed host BPM");
        p.reset();
        check(run(50000, 120).empty() && p.position(token) == -1,
            "host start/reset resumed stale loop");
        auto old = p.publish({{0, 60}}, 1, 120, 4, 4, true);
        auto current = p.publish({{0, 64}}, 1, 120, 4, 4, true);
        p.cancel(old);
        check(run(1, 120).size() == 1 && p.position(current) == 0,
            "old page canceled newer audition");
        p.setLoop(current, false);
        check(run(48000, 120).empty() && p.position(current) == -1,
            "loop-off did not finish current pass");
        auto once = p.publish({{0, 60}}, 1, 120, 4, 4, false);
        check(run(100, 120).size() == 1, "one-shot initial hit");
        p.setLoop(once, true);
        check(run(48000, 120).size() == 2, "enabling loop restarted/lost musical phase");
        p.publish({{0, 60}}, 1, 120, 4, 4, true, true);
        const auto ramp = run(48000, 120, .001);
        check(ramp.size() == 3, "linear host tempo ramp event count");
        for (std::size_t i = 0; i < ramp.size(); ++i) {
            const double f = ramp[i];
            const double rows = (120*f + .001*f*(f-1)/2) * 4 / (60*48000);
            check(rows >= i*4 - 1e-8 && rows < i*4 + .001,
                "tempo ramp onset not sample accurate");
        }
    }
    {
        PreviewSequencer p;
        std::vector<PitchPreviewEvent> dense;
        for (uint32_t i = 0; i < 1024; ++i) dense.push_back({0, uint8_t(36+i%48)});
        dense.push_back({70000, 100}); // long assembly must not wrap at 65535
        auto token = p.publish(dense, 1, 1000, 96, 70002, false);
        p.beginBlock(2100001, 48000, 1000);
        unsigned count = 0;
        PreviewHit hit;
        while (p.next(hit)) { ++count; }
        p.endBlock();
        check(count == 1025 && hit.offset == 2100000, "preview truncated dense/long assembly");
        check(p.position(token) == 70000, "trailing rest ended too early");
        p.beginBlock(60, 48000, 1000);
        while (p.next(hit)) {}
        p.endBlock();
        check(p.position(token) == -1, "one-shot ignored full duration");
    }
    {
        // Keep a fractional final onset across the one-shot duration boundary.
        PreviewSequencer p;
        const auto token = p.publish({{3, 60, 100, 70, 65535}}, 1, 1000, 96, 4);
        p.beginBlock(111, 44100, 1000);
        PreviewHit hit;
        check(!p.next(hit), "last fractional event emitted before its sample");
        p.endBlock();
        p.beginBlock(64, 44100, 1000);
        check(p.next(hit) && hit.offset == 0, "one-shot dropped final quantized event");
        check(!p.next(hit), "one-shot repeated final quantized event");
        p.endBlock();
        check(p.position(token) == -1, "fractional one-shot failed to finish");
    }
    {
        // Exercise publication/reclamation while the consumer reads plans.
        PreviewSequencer p;
        std::atomic<bool> done {false};
        std::thread audio([&] {
            inAudio = true;
            while (!done.load()) {
                p.beginBlock(127, 48000, 123);
                PreviewHit h;
                while (p.next(h)) {}
                p.endBlock();
            }
            p.reset();
            inAudio = false;
        });
        for (unsigned i = 0; i < 20000; ++i) {
            auto token = p.publish({{0, 60}, {3, 64}}, 1, 123, 4, 16, true);
            (void)p.position(token);
            if (i%3 == 0) p.cancel(token);
        }
        done.store(true);
        audio.join();
    }
    check(audioAllocations == 0 && audioDeletions == 0,
        "audio callback allocated/freed memory");
    std::cout << checks << " checks, " << failures << " failures; worst onset error "
        << worstError << " samples\n";
    return failures ? 1 : 0;
}
