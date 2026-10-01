#pragma once
#include <array>
#include <limits>

namespace s3g::decks {
// Audio-thread-only controller gesture state, not saved musical parameters.
struct BeatpadKnob {
    int position=-1;
    unsigned binding=std::numeric_limits<unsigned>::max();
    float written=0;
    bool caught=false;
};
struct BeatpadState {
    std::array<std::array<BeatpadKnob,4>,2> knobs{};
    std::array<std::array<bool,8>,2> storingCue{};
    std::array<bool,2> faderStart{};
    bool filterEq=false;
    void resetGestures() noexcept {
        storingCue={};faderStart={};
        for(auto& deck:knobs)for(auto& knob:deck){knob.binding=std::numeric_limits<unsigned>::max();knob.caught=false;}
    }
    void eqLayout(bool filter) noexcept {
        if(filterEq==filter)return;
        filterEq=filter;
        for(auto& deck:knobs)for(auto& knob:deck){knob.binding=std::numeric_limits<unsigned>::max();knob.caught=false;}
    }
};
} // namespace s3g::decks
