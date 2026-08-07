#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s3g::tracker {

using MidiEndpointId = std::int32_t;
constexpr std::size_t kCoreMidiVirtualSourceCount = 8u;

struct MidiDestination {
  MidiEndpointId id = 0;
  std::string name;
};

enum class MidiOutputTargetKind {
  VirtualSource,
  Destination,
};

struct MidiOutputTarget {
  MidiOutputTargetKind kind = MidiOutputTargetKind::VirtualSource;
  MidiEndpointId destinationId = 0;
  // One based; ignored for physical destinations.
  std::uint8_t virtualSource = 1u;
  std::string name;
};

// Thread-safe CoreMIDI output used by the standalone application.
//
// Channel values are zero based (0..15). Host times use the same mach absolute
// clock as CoreMIDI's MIDITimeStamp. A host time of zero requests immediate
// delivery and is normalized to the current mach time (required for virtual
// sources as well as direct destinations). Calls are serialized internally,
// but they are not real-time safe:
// endpoint refreshes and error reporting may allocate.
class CoreMidiOutput final {
 public:
  explicit CoreMidiOutput(
      std::string virtualSourceName = "s3g Tracker");
  ~CoreMidiOutput();

  CoreMidiOutput(const CoreMidiOutput&) = delete;
  CoreMidiOutput& operator=(const CoreMidiOutput&) = delete;
  CoreMidiOutput(CoreMidiOutput&&) noexcept;
  CoreMidiOutput& operator=(CoreMidiOutput&&) noexcept;

  // Returns currently connected MIDI destinations. The stable CoreMIDI
  // kMIDIPropertyUniqueID is used as MidiDestination::id.
  std::vector<MidiDestination> destinations();

  // Source 1 is the legacy/default target. MIDI applications can subscribe to
  // all eight inputs named virtualSourceName(1)..virtualSourceName(8).
  bool selectVirtualSource(std::string* error = nullptr);
  bool selectDestination(MidiEndpointId id, std::string* error = nullptr);
  MidiOutputTarget selectedTarget() const;

  bool isReady() const;
  bool isTargetReady(const MidiOutputTarget& target);
  std::string virtualSourceName(std::uint8_t oneBasedIndex = 1u) const;
  std::string lastError() const;

  bool sendNoteOn(std::uint8_t channel,
                  std::uint8_t note,
                  std::uint8_t velocity,
                  std::uint64_t hostTime,
                  std::string* error = nullptr);
  bool sendNoteOff(std::uint8_t channel,
                   std::uint8_t note,
                   std::uint8_t velocity,
                   std::uint64_t hostTime,
                   std::string* error = nullptr);
  bool sendControlChange(std::uint8_t channel,
                         std::uint8_t controller,
                         std::uint8_t value,
                         std::uint64_t hostTime,
                         std::string* error = nullptr);

  bool sendNoteOnTo(const MidiOutputTarget& target,
                    std::uint8_t channel,
                    std::uint8_t note,
                    std::uint8_t velocity,
                    std::uint64_t hostTime,
                    std::string* error = nullptr);
  bool sendNoteOffTo(const MidiOutputTarget& target,
                     std::uint8_t channel,
                     std::uint8_t note,
                     std::uint8_t velocity,
                     std::uint64_t hostTime,
                     std::string* error = nullptr);
  bool sendControlChangeTo(const MidiOutputTarget& target,
                           std::uint8_t channel,
                           std::uint8_t controller,
                           std::uint8_t value,
                           std::uint64_t hostTime,
                           std::string* error = nullptr);
  bool panicTarget(const MidiOutputTarget& target,
                   std::string* error = nullptr);

  // Sends CC 123 (All Notes Off) immediately on all sixteen MIDI channels.
  bool panic(std::string* error = nullptr);

  static std::uint64_t hostTimeNow() noexcept;
  static std::uint64_t hostTicksForSeconds(double seconds) noexcept;
  static std::uint64_t addSecondsToHostTime(std::uint64_t base,
                                            double seconds) noexcept;
  static double secondsBetweenHostTimes(std::uint64_t earlier,
                                        std::uint64_t later) noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace s3g::tracker
