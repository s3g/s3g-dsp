#pragma once

#include <cstdint>

namespace s3g::nim_gesture_midi {

// Human-facing MIDI channels 16 and 15 respectively. Commands and feedback
// deliberately use different wire channels so an accidental MIDI loop cannot
// turn a state update into another transport command.
constexpr uint8_t kCommandChannel = 15u;
constexpr uint8_t kFeedbackChannel = 14u;

constexpr uint8_t kRecordNote = 112u;
constexpr uint8_t kPlayNote = 113u;
constexpr uint8_t kClearLastNote = 114u;
constexpr uint8_t kClearAllNote = 115u;
constexpr uint8_t kCancelRecordNote = 116u;

constexpr bool isCommandNote(uint8_t note)
{
    return note >= kRecordNote && note <= kCancelRecordNote;
}

constexpr bool isFeedbackMessage(
    uint8_t status, uint8_t note)
{
    const uint8_t command = status & 0xf0u;
    const uint8_t channel = status & 0x0fu;
    return channel == kFeedbackChannel
        && (command == 0x80u || command == 0x90u)
        && isCommandNote(note & 0x7fu);
}

} // namespace s3g::nim_gesture_midi
