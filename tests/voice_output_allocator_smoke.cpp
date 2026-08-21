#include "s3g_voice_output_allocator.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using s3g::routing::OutputTraversal;
using s3g::routing::OutputVoiceWidth;
using s3g::routing::StereoPairLayout;
using s3g::routing::TriggerOutputAllocator;
using s3g::routing::VoiceOutputRouting;

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    TriggerOutputAllocator<32u> allocator;
    VoiceOutputRouting routing;
    routing.width = OutputVoiceWidth::Mono;

    for (uint32_t trigger = 0u; trigger < 33u; ++trigger) {
        const auto assignment = allocator.next(32u, routing);
        ok &= expect(assignment.channelCount == 1u
                && assignment.firstChannel == trigger % 32u,
            "sequential mono routing did not traverse all 32 channels");
    }

    allocator.reset();
    routing.traversal = OutputTraversal::ReverseSequential;
    for (uint32_t trigger = 0u; trigger < 32u; ++trigger) {
        const auto assignment = allocator.next(32u, routing);
        ok &= expect(assignment.firstChannel == 31u - trigger,
            "reverse sequential routing order is incorrect");
    }

    allocator.reset();
    routing.traversal = OutputTraversal::Palindrome;
    constexpr std::array<uint8_t, 8u> palindrome {{
        0u, 1u, 2u, 3u, 2u, 1u, 0u, 1u,
    }};
    for (uint8_t expected : palindrome)
        ok &= expect(allocator.next(4u, routing).firstChannel == expected,
            "palindrome routing repeated an endpoint or changed direction incorrectly");

    allocator.reset(0x12345678u);
    routing.traversal = OutputTraversal::Random;
    uint32_t randomMask = 0u;
    for (uint32_t trigger = 0u; trigger < 64u; ++trigger) {
        const auto assignment = allocator.next(8u, routing);
        ok &= expect(assignment.firstChannel < 8u,
            "random routing produced an out-of-range channel");
        randomMask |= 1u << assignment.firstChannel;
    }
    ok &= expect((randomMask & (randomMask - 1u)) != 0u,
        "random routing failed to vary its destination");

    allocator.reset(0x87654321u);
    routing.traversal = OutputTraversal::RandomCycle;
    uint32_t prior = 0xffffffffu;
    for (uint32_t cycle = 0u; cycle < 3u; ++cycle) {
        uint32_t mask = 0u;
        for (uint32_t trigger = 0u; trigger < 16u; ++trigger) {
            const auto assignment = allocator.next(16u, routing);
            const uint32_t channel = assignment.firstChannel;
            ok &= expect(channel < 16u && (mask & (1u << channel)) == 0u,
                "random-cycle routing repeated before exhausting its destinations");
            if (trigger == 0u && prior != 0xffffffffu)
                ok &= expect(channel != prior,
                    "random-cycle routing repeated at a bag boundary");
            mask |= 1u << channel;
            prior = channel;
        }
        ok &= expect(mask == 0xffffu,
            "random-cycle routing did not visit every destination");
    }

    allocator.reset();
    routing = {};
    routing.width = OutputVoiceWidth::Stereo;
    routing.pairLayout = StereoPairLayout::Adjacent;
    auto pair = allocator.next(32u, routing);
    ok &= expect(pair.channelCount == 2u && pair.firstChannel == 0u
            && pair.secondChannel == 1u,
        "adjacent stereo pair zero is incorrect");
    pair = allocator.next(32u, routing);
    ok &= expect(pair.firstChannel == 2u && pair.secondChannel == 3u,
        "adjacent stereo pair traversal is incorrect");

    allocator.reset();
    routing.pairLayout = StereoPairLayout::SplitBanks;
    pair = allocator.next(32u, routing);
    ok &= expect(pair.firstChannel == 0u && pair.secondChannel == 16u,
        "split-bank stereo pair zero is incorrect");
    pair = allocator.next(32u, routing);
    ok &= expect(pair.firstChannel == 1u && pair.secondChannel == 17u,
        "split-bank stereo pair traversal is incorrect");

    allocator.reset();
    routing.traversal = OutputTraversal::Sequential;
    routing.pairLayout = StereoPairLayout::Adjacent;
    pair = allocator.next(8u, routing);
    ok &= expect(pair.firstChannel == 0u && pair.secondChannel == 1u,
        "an eight-channel active width did not begin at pair 1/2");
    pair = allocator.next(8u, routing);
    pair = allocator.next(8u, routing);
    pair = allocator.next(8u, routing);
    ok &= expect(pair.firstChannel == 6u && pair.secondChannel == 7u,
        "an eight-channel active width did not reach pair 7/8");
    pair = allocator.next(8u, routing);
    ok &= expect(pair.firstChannel == 0u && pair.secondChannel == 1u,
        "an eight-channel active width did not wrap after four pairs");

    allocator.reset();
    routing.pairLayout = StereoPairLayout::SplitBanks;
    pair = allocator.next(7u, routing);
    ok &= expect(pair.firstChannel == 0u && pair.secondChannel == 3u,
        "odd active widths did not retain an in-range split-bank pair");
    pair = allocator.next(7u, routing);
    pair = allocator.next(7u, routing);
    ok &= expect(pair.firstChannel == 2u && pair.secondChannel == 5u,
        "odd active widths did not leave only the unmatched final channel idle");

    if (!ok) return 1;
    std::cout << "s3g trigger output allocator smoke: ok\n";
    return 0;
}
