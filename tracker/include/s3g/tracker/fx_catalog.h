#pragma once

#include "s3g/tracker/sequencer.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace s3g::tracker {

// Initial compiled action catalog for the membrane rack. Catalog actions are
// lane-relative: the scheduler resolves kTrackInstrumentNode to the lane's
// current remembered instrument node before duplicate suppression and event
// emission.
// Authoring/UI code therefore stays independent of a particular rack slot.
struct FxParameterActionDefinition {
    uint32_t targetNode = kTrackInstrumentNode;
    uint32_t parameterId = 0u;
    std::string_view stableKey;
    std::string_view mnemonic;
    std::string_view displayName;
    // Bit zero/one/two correspond to Global/Channel/Note. Each membrane rack
    // instance has one global parameter state, so these catalog entries
    // intentionally expose Global only.
    uint8_t supportedScopeMask = 1u;
};

struct SequencerActionDefinition {
    SequencerAction action = SequencerAction::Ratchet;
    std::string_view stableKey;
    std::string_view mnemonic;
    std::string_view displayName;
    std::string_view valueMeaning;
};

constexpr uint8_t fxScopeBit(ParameterScope scope) noexcept
{
    return static_cast<uint8_t>(1u
        << static_cast<uint8_t>(scope));
}

constexpr bool fxActionSupportsScope(
    const FxParameterActionDefinition& action,
    ParameterScope scope) noexcept
{
    return (action.supportedScopeMask & fxScopeBit(scope)) != 0u;
}

std::size_t fxParameterActionCount() noexcept;
const FxParameterActionDefinition* fxParameterAction(
    std::size_t index) noexcept;
const FxParameterActionDefinition* findFxParameterAction(
    std::string_view stableKey) noexcept;
const FxParameterActionDefinition* findFxParameterAction(
    uint32_t targetNode, uint32_t parameterId) noexcept;

std::size_t sequencerActionCount() noexcept;
const SequencerActionDefinition* sequencerAction(
    std::size_t index) noexcept;
const SequencerActionDefinition* findSequencerAction(
    std::string_view stableKeyOrMnemonic) noexcept;
const SequencerActionDefinition* findSequencerAction(
    SequencerAction action) noexcept;

} // namespace s3g::tracker
