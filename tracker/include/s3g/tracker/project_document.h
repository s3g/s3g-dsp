#pragma once

#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/pattern_bank.h"
#include "s3g/tracker/phrase_library.h"
#include "s3g/tracker/sequencer.h"
#include "s3g/tracker/song_playback_planner.h"
#include "s3g/tracker/tracker_follow.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s3g::tracker {

// The native file is a MIDI-composition document, not a snapshot of the
// retired internal instrument rack. Version 3 is intentionally a breaking
// boundary: the decoder accepts this exact format/version pair and performs no
// migration from the former hybrid tracker/instrument schemas.
constexpr uint32_t kProjectFormatVersion = 3u;
constexpr const char* kProjectFormatIdentifier
    = "s3g-tracker-midi-composition";
constexpr const char* kProjectFileExtension = ".s3gt";

constexpr std::size_t kMaximumAssemblyBlocks = 64u;
constexpr uint32_t kMaximumAssemblyBlockRepeats = 64u;

enum class AssemblyPlacementMode : uint8_t {
    Replace,
    MergeIntoEmpty,
};

enum class AssemblyFitMode : uint8_t {
    ExtendPattern,
    Crop,
    Wrap,
};

// Project-persistent staging recipe for the ASSEMBLE page. Blocks reference
// reusable Phrase assets only while they are in the tray. Placing the tray
// materializes ordinary Tracker cells, leaving no playback dependency.
struct PhraseAssemblyBlock {
    AssetBankId phraseBankId = kProjectAssetBankId;
    uint32_t phraseSlot = 0u;
    uint32_t repeats = 1u;
};

struct PhraseAssemblyDraft {
    std::vector<PhraseAssemblyBlock> blocks;
    std::string targetPatternId;
    uint32_t targetTrack = 0u;
    uint32_t targetRow = 0u;
    uint8_t previewMidiChannel = 1u;
    AssemblyPlacementMode placementMode = AssemblyPlacementMode::Replace;
    AssemblyFitMode fitMode = AssemblyFitMode::ExtendPattern;
    bool loopPreview = false;
};

struct ProjectSessionState {
    double gateMilliseconds = 90.0;
    // Musical rate applied to the host tempo by the CLAP tracker. Values are
    // normalized by the UI to the supported ratio menu.
    double tempoScale = 1.0;
    // Pattern transport remains the default. Enabling Song transport is an
    // explicit project choice so merely opening the Song editor never changes
    // the behavior of Play.
    bool songPlaybackEnabled = false;
    // This is presentation state rather than musical data, but it belongs to
    // the native composition so reopening it restores the author's working
    // view.
    bool showMidiNoteValues = true;
    // Project-scoped Tracker navigation preference. Up/Down move this many
    // rows, making sparse entry (for example every third row) immediate.
    uint32_t trackerRowJump = 1u;
    TrackerFollowSettings trackerFollow;
    // Stored as a decimal string by the JSON codec so all 64 bits survive a
    // round trip through tools whose JSON number type is IEEE double.
    uint64_t commandRngState = 0x7333672d74726163ull;
    // Seed consumed by deterministic playback-time probability/generative
    // actions. This remains distinct from the transactional command RNG.
    uint32_t playbackSeed = 0x6d2b79f5u;
    AssetBankId activeBurstBankId = kProjectAssetBankId;
    AssetBankId activePhraseBankId = kProjectAssetBankId;
    PhraseAssemblyDraft assembly;
};

struct ProjectDocument {
    PatternBank patternBank = makeDefaultPatternBank();
    std::vector<BurstBank> burstBanks { makeProjectBurstBank() };
    std::vector<PhraseBank> phraseBanks { makeProjectPhraseBank() };
    TransportSettings transport;
    TimingWarpLibrary warpLibrary;
    ProjectSessionState session;
    InstrumentRackState instrumentRack = makeDefaultInstrumentRack();
    SongArrangement song;
};

inline AssetBankId nextAssetBankId(const ProjectDocument& document) noexcept
{
    AssetBankId next = kProjectAssetBankId + 1u;
    for (const auto& bank : document.burstBanks)
        if (bank.id >= next) next = bank.id + 1u;
    for (const auto& bank : document.phraseBanks)
        if (bank.id >= next) next = bank.id + 1u;
    return next == kInvalidAssetBankId ? kProjectAssetBankId + 1u : next;
}

} // namespace s3g::tracker
