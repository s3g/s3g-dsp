#include "s3g/tracker/fx_catalog.h"

#include <array>

namespace s3g::tracker {
namespace {

constexpr std::array<FxParameterActionDefinition, 19u> kActions {{
    { kTrackInstrumentNode, 2u, "membrane.shape", "SHP", "Membrane Shape" },
    { kTrackInstrumentNode, 3u, "membrane.tune", "TUN", "Fundamental" },
    { kTrackInstrumentNode, 4u, "membrane.pitch_drop", "PDR", "Pitch Drop" },
    { kTrackInstrumentNode, 5u, "membrane.pitch_time", "PTM", "Pitch Drop Time" },
    { kTrackInstrumentNode, 6u, "membrane.decay", "DEC", "Decay" },
    { kTrackInstrumentNode, 7u, "membrane.damping", "DMP", "Damping" },
    { kTrackInstrumentNode, 8u, "membrane.punch", "PUN", "Punch" },
    { kTrackInstrumentNode, 9u, "membrane.click", "CLK", "Click" },
    { kTrackInstrumentNode, 10u, "membrane.drive", "DRV", "Drive" },
    { kTrackInstrumentNode, 11u, "membrane.strike_x", "STX", "Strike X" },
    { kTrackInstrumentNode, 12u, "membrane.strike_y", "STY", "Strike Y" },
    { kTrackInstrumentNode, 13u, "membrane.spread", "SPR", "Spatial Spread" },
    { kTrackInstrumentNode, 14u, "membrane.depth", "DEP", "Membrane Depth" },
    { kTrackInstrumentNode, 15u, "membrane.rotation", "ROT", "Rotation" },
    { kTrackInstrumentNode, 16u, "membrane.shape_amount", "SHA", "Shape Amount" },
    { kTrackInstrumentNode, 17u, "membrane.velocity", "VSN", "Velocity Sensitivity" },
    { kTrackInstrumentNode, 18u, "membrane.note_tracking", "NTR", "Note Tracking" },
    { kTrackInstrumentNode, 19u, "membrane.output", "OUT", "Output Gain" },
    { kTrackInstrumentNode, 21u, "membrane.strike_placement", "STP", "Strike Placement" },
}};

constexpr std::array<SequencerActionDefinition,
    kSequencerActionCount> kSequencerActions {{
    { SequencerAction::Ratchet, "seq.ratchet", "RR", "Ratchet",
        "2 to 8 evenly spaced onsets within one nominal tempo tick" },
    { SequencerAction::MicroTime, "seq.microtime", "MT", "Microtime",
        "early to late around the compensated center" },
    { SequencerAction::Delay, "seq.delay", "DL", "Delay",
        "zero to one full tracker tick" },
    { SequencerAction::Flam, "seq.flam", "FL", "Flam",
        "one quieter secondary onset 6 to 60 milliseconds later" },
    { SequencerAction::Stutter, "seq.stutter", "ST", "Stutter",
        "2 to 8 onsets extending across future ticks" },
    { SequencerAction::Accent, "seq.accent", "AC", "Accent",
        "0.5x to 1.5x onset velocity" },
    { SequencerAction::Ghost, "seq.ghost", "GL", "Ghost",
        "one quiet secondary onset at half a tick" },
    { SequencerAction::Probability, "seq.probability", "PR", "Probability",
        "deterministic zero-to-one probability gate before timing expansion" },
    { SequencerAction::Skip, "seq.skip", "SK", "Skip",
        "play one occurrence per two-to-eight visits of this NOTE row" },
    { SequencerAction::Offset, "seq.offset", "OF", "Note Offset",
        "read the NOTE source from four rows back through four rows ahead" },
    { SequencerAction::RepeatPrevious, "seq.repeat_previous", "RP",
        "Repeat Previous",
        "fill an empty NOTE source with the last emitted note by probability" },
    { SequencerAction::Euclid, "seq.euclid", "EU", "Euclidean Gate",
        "gate the NOTE row with one through NOTE-length Euclidean hits" },
    { SequencerAction::WarpRecall, "seq.warp_recall", "WRP", "Warp Recall",
        "recall composed timing-warp library slot 01 through 64" },
}};

bool equalFold(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0u; index < left.size(); ++index) {
        const char a = left[index] >= 'A' && left[index] <= 'Z'
            ? static_cast<char>(left[index] - 'A' + 'a') : left[index];
        const char b = right[index] >= 'A' && right[index] <= 'Z'
            ? static_cast<char>(right[index] - 'A' + 'a') : right[index];
        if (a != b) return false;
    }
    return true;
}

} // namespace

std::size_t fxParameterActionCount() noexcept { return kActions.size(); }

const FxParameterActionDefinition* fxParameterAction(
    std::size_t index) noexcept
{
    return index < kActions.size() ? &kActions[index] : nullptr;
}

const FxParameterActionDefinition* findFxParameterAction(
    std::string_view stableKey) noexcept
{
    for (const auto& action : kActions) {
        if (action.stableKey == stableKey) return &action;
    }
    return nullptr;
}

const FxParameterActionDefinition* findFxParameterAction(
    uint32_t targetNode, uint32_t parameterId) noexcept
{
    for (const auto& action : kActions) {
        if (action.targetNode == targetNode
            && action.parameterId == parameterId) return &action;
    }
    return nullptr;
}

std::size_t sequencerActionCount() noexcept
{
    return kSequencerActions.size();
}

const SequencerActionDefinition* sequencerAction(
    std::size_t index) noexcept
{
    return index < kSequencerActions.size()
        ? &kSequencerActions[index] : nullptr;
}

const SequencerActionDefinition* findSequencerAction(
    std::string_view key) noexcept
{
    for (const auto& action : kSequencerActions) {
        if (equalFold(action.stableKey, key)
            || equalFold(action.mnemonic, key)) return &action;
        constexpr std::string_view prefix = "seq.";
        if (action.stableKey.size() > prefix.size()
            && equalFold(action.stableKey.substr(prefix.size()), key))
            return &action;
    }
    return nullptr;
}

const SequencerActionDefinition* findSequencerAction(
    SequencerAction requested) noexcept
{
    for (const auto& action : kSequencerActions) {
        if (action.action == requested) return &action;
    }
    return nullptr;
}

} // namespace s3g::tracker
