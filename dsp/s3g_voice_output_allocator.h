#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace s3g::routing {

enum class OutputTraversal : uint8_t {
    Sequential = 0u,
    ReverseSequential,
    Palindrome,
    Random,
    RandomCycle,
};

enum class OutputVoiceWidth : uint8_t {
    Mono = 0u,
    Stereo,
};

enum class StereoPairLayout : uint8_t {
    Adjacent = 0u,
    SplitBanks,
};

struct VoiceOutputRouting {
    OutputTraversal traversal = OutputTraversal::Sequential;
    OutputVoiceWidth width = OutputVoiceWidth::Stereo;
    StereoPairLayout pairLayout = StereoPairLayout::Adjacent;
    bool avoidAdjacent = false;

    bool valid() const noexcept
    {
        return static_cast<uint8_t>(traversal)
                <= static_cast<uint8_t>(OutputTraversal::RandomCycle)
            && static_cast<uint8_t>(width)
                <= static_cast<uint8_t>(OutputVoiceWidth::Stereo)
            && static_cast<uint8_t>(pairLayout)
                <= static_cast<uint8_t>(StereoPairLayout::SplitBanks);
    }
};

struct VoiceOutputAssignment {
    uint8_t firstChannel = 0u;
    uint8_t secondChannel = 0u;
    uint8_t channelCount = 1u;
    uint8_t destination = 0u;
};

// TriggerOutputAllocator is deliberately independent of any voice or sample
// engine. Instruments ask for one assignment at note start and retain the
// returned channels for that voice's lifetime.
template <std::size_t MaximumChannels = 32u>
class TriggerOutputAllocator {
public:
    static_assert(MaximumChannels > 0u && MaximumChannels <= 255u,
        "output allocator channel count must fit its public assignment");

    void reset(uint32_t seed = 0x6d2b79f5u) noexcept
    {
        sequence_ = 0u;
        palindromePosition_ = 0u;
        palindromeForward_ = true;
        randomState_ = seed != 0u ? seed : 0x6d2b79f5u;
        bagSize_ = 0u;
        bagPosition_ = 0u;
        lastDestination_ = invalidDestination();
        signature_ = 0u;
    }

    VoiceOutputAssignment next(uint32_t outputChannelCount,
        const VoiceOutputRouting& routing) noexcept
    {
        outputChannelCount = std::clamp<uint32_t>(outputChannelCount, 1u,
            static_cast<uint32_t>(MaximumChannels));
        const uint32_t voiceWidth = routing.width == OutputVoiceWidth::Stereo
            && outputChannelCount >= 2u ? 2u : 1u;
        const uint32_t destinationCount = std::max(1u,
            outputChannelCount / voiceWidth);
        const uint32_t signature = outputChannelCount
            | (voiceWidth << 8u)
            | (static_cast<uint32_t>(routing.traversal) << 12u)
            | (static_cast<uint32_t>(routing.pairLayout) << 16u)
            | (static_cast<uint32_t>(routing.avoidAdjacent) << 20u);
        if (signature != signature_) {
            sequence_ = 0u;
            palindromePosition_ = 0u;
            palindromeForward_ = true;
            bagSize_ = 0u;
            bagPosition_ = 0u;
            lastDestination_ = invalidDestination();
            signature_ = signature;
        }

        uint32_t destination = 0u;
        switch (routing.traversal) {
        case OutputTraversal::ReverseSequential:
            destination = destinationCount - 1u
                - sequence_++ % destinationCount;
            break;
        case OutputTraversal::Palindrome:
            destination = palindromePosition_;
            advancePalindrome(destinationCount);
            break;
        case OutputTraversal::Random:
            destination = nextRandom() % destinationCount;
            if (routing.avoidAdjacent && destinationCount > 1u) {
                for (uint32_t attempt = 0u; attempt < 16u
                     && destinationIsNear(destination); ++attempt)
                    destination = nextRandom() % destinationCount;
                if (destinationIsNear(destination))
                    destination = firstDistantDestination(destinationCount);
            }
            break;
        case OutputTraversal::RandomCycle:
            if (bagSize_ != destinationCount || bagPosition_ >= bagSize_)
                refillBag(destinationCount, routing.avoidAdjacent);
            if (routing.avoidAdjacent && destinationCount > 1u
                && destinationIsNear(bag_[bagPosition_])) {
                for (uint32_t index = bagPosition_ + 1u;
                     index < bagSize_; ++index) {
                    if (!destinationIsNear(bag_[index])) {
                        std::swap(bag_[bagPosition_], bag_[index]);
                        break;
                    }
                }
            }
            destination = bag_[bagPosition_++];
            break;
        case OutputTraversal::Sequential:
        default:
            destination = sequence_++ % destinationCount;
            break;
        }
        lastDestination_ = destination;

        VoiceOutputAssignment result;
        result.destination = static_cast<uint8_t>(destination);
        result.channelCount = static_cast<uint8_t>(voiceWidth);
        if (voiceWidth == 1u) {
            result.firstChannel = static_cast<uint8_t>(destination);
            result.secondChannel = result.firstChannel;
        } else if (routing.pairLayout == StereoPairLayout::SplitBanks) {
            result.firstChannel = static_cast<uint8_t>(destination);
            result.secondChannel = static_cast<uint8_t>(destination
                + destinationCount);
        } else {
            result.firstChannel = static_cast<uint8_t>(destination * 2u);
            result.secondChannel = static_cast<uint8_t>(destination * 2u + 1u);
        }
        return result;
    }

private:
    static constexpr uint32_t invalidDestination() noexcept
    { return 0xffffffffu; }

    uint32_t nextRandom() noexcept
    {
        uint32_t value = randomState_;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        randomState_ = value != 0u ? value : 0x6d2b79f5u;
        return randomState_;
    }

    bool destinationIsNear(uint32_t destination) const noexcept
    {
        if (lastDestination_ == invalidDestination()) return false;
        const uint32_t distance = destination > lastDestination_
            ? destination - lastDestination_ : lastDestination_ - destination;
        return distance <= 1u;
    }

    uint32_t firstDistantDestination(uint32_t destinationCount) const noexcept
    {
        for (uint32_t destination = 0u; destination < destinationCount;
             ++destination)
            if (!destinationIsNear(destination)) return destination;
        for (uint32_t destination = 0u; destination < destinationCount;
             ++destination)
            if (destination != lastDestination_) return destination;
        return 0u;
    }

    void advancePalindrome(uint32_t destinationCount) noexcept
    {
        if (destinationCount <= 1u) return;
        if (palindromeForward_) {
            if (palindromePosition_ + 1u >= destinationCount) {
                palindromeForward_ = false;
                --palindromePosition_;
            } else {
                ++palindromePosition_;
            }
        } else if (palindromePosition_ == 0u) {
            palindromeForward_ = true;
            ++palindromePosition_;
        } else {
            --palindromePosition_;
        }
    }

    void refillBag(uint32_t destinationCount, bool avoidAdjacent) noexcept
    {
        bagSize_ = destinationCount;
        bagPosition_ = 0u;
        const auto shuffle = [&]() {
            for (uint32_t index = 0u; index < destinationCount; ++index)
                bag_[index] = static_cast<uint8_t>(index);
            for (uint32_t index = destinationCount; index > 1u; --index) {
                const uint32_t swapWith = nextRandom() % index;
                std::swap(bag_[index - 1u], bag_[swapWith]);
            }
        };
        const auto bagAvoidsAdjacent = [&]() {
            if (destinationCount <= 1u) return true;
            if (lastDestination_ != invalidDestination()) {
                const uint32_t first = bag_[0u];
                const uint32_t distance = first > lastDestination_
                    ? first - lastDestination_ : lastDestination_ - first;
                if (distance <= 1u) return false;
            }
            for (uint32_t index = 1u; index < destinationCount; ++index) {
                const uint32_t left = bag_[index - 1u];
                const uint32_t right = bag_[index];
                const uint32_t distance = left > right
                    ? left - right : right - left;
                if (distance <= 1u) return false;
            }
            return true;
        };
        shuffle();
        if (avoidAdjacent && destinationCount >= 4u) {
            for (uint32_t attempt = 0u; attempt < 128u
                 && !bagAvoidsAdjacent(); ++attempt) shuffle();
        }
        if (destinationCount > 1u
            && lastDestination_ != invalidDestination()
            && bag_[0u] == lastDestination_) {
            const uint32_t swapWith = 1u
                + nextRandom() % (destinationCount - 1u);
            std::swap(bag_[0u], bag_[swapWith]);
        }
    }

    uint32_t sequence_ = 0u;
    uint32_t palindromePosition_ = 0u;
    bool palindromeForward_ = true;
    uint32_t randomState_ = 0x6d2b79f5u;
    std::array<uint8_t, MaximumChannels> bag_ {};
    uint32_t bagSize_ = 0u;
    uint32_t bagPosition_ = 0u;
    uint32_t lastDestination_ = invalidDestination();
    uint32_t signature_ = 0u;
};

} // namespace s3g::routing
