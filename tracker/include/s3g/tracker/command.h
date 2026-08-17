#pragma once

#include "s3g/tracker/sequencer.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s3g::tracker {

// Mutable, UI-owned state exposed to the live console. Playback remains the
// responsibility of the app coordinator: a successful command reports which
// parts of this state changed and which transport action (if any) was asked
// for.
struct TrackerSession {
    Pattern pattern;
    TransportSettings transport;
    TimingWarpLibrary warpLibrary;
    double gateMilliseconds = 90.0;
    // Console aliases are stored without the leading '@' and point to
    // zero-based lanes. std::map keeps the `aliases` response deterministic.
    std::map<std::string, std::size_t> aliases;
    // A rest-only drum lane otherwise has nowhere to retain the pitch that a
    // later mask hit should use. Kit commands maintain these per-lane anchors.
    std::vector<uint8_t> laneDefaultNotes;
    // Transactional deterministic PRNG state for stochastic pattern commands.
    // Successful density/thin/humanize commands consume this state as needed;
    // rejected commands leave it untouched with the rest of the session.
    uint64_t commandRngState = 0x7333672d74726163ull;
    // Stable seed for render-time probability and generative FX. Kept apart
    // from commandRngState so editing commands cannot perturb playback.
    uint32_t playbackSeed = 0x6d2b79f5u;
    // Zero-based internally. Console lane and row arguments are one-based.
    std::size_t selectedTrack = 0u;
    std::size_t selectedRow = 0u;
    // Three compact grid pages keep eight lanes readable: NOTE/INS/VEL,
    // FX1/V1, and FX2/V2. Page zero has three fields; FX pages have two.
    std::size_t selectedPage = 0u;
    std::size_t selectedField = 0u;
};

enum class CommandEffect : uint32_t {
    None = 0u,
    PatternChanged = 1u << 0u,
    TransportChanged = 1u << 1u,
    SelectionChanged = 1u << 2u,
    StartPlayback = 1u << 3u,
    StopPlayback = 1u << 4u,
    Panic = 1u << 5u,
    OutputChanged = 1u << 6u,
    RoutingChanged = 1u << 7u,
    // Persistent session state changed without necessarily changing the
    // pattern or transport (for example, a console alias binding).
    ProjectChanged = 1u << 8u,
    // Project-level history is owned by the application coordinator rather
    // than TrackerSession, so console commands report explicit requests.
    UndoRequested = 1u << 9u,
    RedoRequested = 1u << 10u,
};

enum class PatternVariationLaunch : uint8_t {
    None,
    NextTick,
    NextBeat,
    NextPatternCycle,
};

bool patternVariationLaunchIsDue(PatternVariationLaunch launch,
    uint64_t completedTickIndex, uint64_t completedTransportRow,
    uint32_t ticksPerBeat, std::size_t patternRows) noexcept;

// Bank-level requests are parsed and generated transactionally by the shared
// command engine, then installed by the application coordinator that owns the
// PatternBank. Keeping the generated session here avoids reparsing or drawing
// from the random stream twice.
struct PatternVariationRequest {
    TrackerSession generatedSession;
    PatternVariationLaunch launch = PatternVariationLaunch::None;
    std::string sourceCommand;
};

constexpr CommandEffect operator|(CommandEffect left,
    CommandEffect right) noexcept
{
    return static_cast<CommandEffect>(static_cast<uint32_t>(left)
        | static_cast<uint32_t>(right));
}

constexpr CommandEffect operator&(CommandEffect left,
    CommandEffect right) noexcept
{
    return static_cast<CommandEffect>(static_cast<uint32_t>(left)
        & static_cast<uint32_t>(right));
}

constexpr CommandEffect& operator|=(CommandEffect& left,
    CommandEffect right) noexcept
{
    left = left | right;
    return left;
}

struct CommandResult {
    bool ok = false;
    CommandEffect effects = CommandEffect::None;
    std::string message;
    std::optional<PatternVariationRequest> patternVariation;

    bool hasEffect(CommandEffect effect) const noexcept
    {
        return (effects & effect) != CommandEffect::None;
    }
};

// Shared command documentation used by both the console's `help` response and
// the native Console Help window. `acceptedVerbs` is a space-separated audit
// field: it names every top-level spelling handled by the corresponding
// entry. The special `@` token covers alias-first shorthand.
struct CommandHelpEntry {
    std::string_view syntax;
    std::string_view description;
    std::string_view acceptedVerbs;
    // One valid invocation of this specific command row.
    std::string_view example;
};

struct CommandHelpSection {
    std::string_view title;
    std::vector<CommandHelpEntry> entries;
};

class CommandEngine {
public:
    // Parse and apply one command. Invalid input never changes the session.
    // Supported commands are summarized by "help". This is a deliberately
    // small native language, not a compatibility layer for the Max console.
    static CommandResult execute(TrackerSession& session,
        std::string_view command);

    static std::string helpText();
    static const std::vector<CommandHelpSection>& helpSections();
};

} // namespace s3g::tracker
