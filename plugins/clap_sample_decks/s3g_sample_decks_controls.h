#pragma once
#include <atomic>
#include <cstdint>

namespace s3g::decks {
// Device adapters translate their protocol into these actions or the existing
// validated parameter IDs. The editor uses the same actions, never fake MIDI.
// Keep profile values stable: future devices append entries, not reinterpret 0/1.
enum class ControllerProfile : unsigned { Notes=0, Beatpad2=1 };
enum class CommandKind { Play, Stop, Seek, Touch, Jog, StackJog, Pad, Record, Cue, SetCue, Sync, ScreenPad, PadPressure,
    StoreBookmark, ClearBookmark, TimedCapture, NextTake, AuditionTake, LaunchTake, FollowSource, Bend, CueStop };
struct Command { CommandKind kind; unsigned deck=0,index=0; double value=0; };

// One atomic word per physical pad: no main-thread reads of audio-owned state.
// Owner mode/bank/role remain latched until the press is released. A short pulse
// makes even a press + release in the same audio block visible to the editor.
struct PadFeedback {
    bool held=false,pulse=false,keyboard=false;
    unsigned mode=0,bank=0;
    float velocity=0,pressure=0;
    uint32_t packed() const noexcept {
        return unsigned(held)|(mode<<1)|(bank<<4)|(unsigned(keyboard)<<6)
            |(unsigned(velocity*127.f+.5f)<<7)|(unsigned(pressure*127.f+.5f)<<14)|(unsigned(pulse)<<21);
    }
    static PadFeedback unpack(uint32_t v) noexcept {
        return {bool(v&1),bool(v&(1u<<21)),bool(v&64),(v>>1)&7,(v>>4)&3,
            float((v>>7)&127)/127.f,float((v>>14)&127)/127.f};
    }
    bool matches(unsigned page,unsigned selectedBank,bool keys) const noexcept {
        return mode==page&&(!(page==3||page==4||page==6)||bank==selectedBank)&&(page!=4||keyboard==keys);
    }
};
} // namespace s3g::decks
