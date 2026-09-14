#pragma once
#include "s3g/tracker/song_playback_planner.h"
#include <functional>

namespace s3g::tracker::editor {
struct SongPatternInfo {
    std::string id, name;
    uint32_t length = 16, lanes = 32;
};
enum class SongField { Pattern, Warp, LoopIn, LoopOut, Repeats, Ticks, Tempo, Energy };
struct SongChoice {
    std::string title, pattern;
    double value = 0;
    bool selected = false;
};
struct SongEditorCallbacks {
    std::function<void()> changed;
    std::function<void(bool)> modeChanged, loopChanged;
    std::function<void(std::size_t, SongLaunchQuantization)> launch;
    std::function<void()> saveProject, loadProject;
};
// UI-thread arrangement authoring. No scheduling, launch execution, or clocks.
// The coordinator retains control of deferred runtime publication during play.
class SongEditor {
public:
    SongEditor();
    SongEditorCallbacks callbacks;
    SongArrangement arrangement;
    std::vector<SongPatternInfo> patterns { { "A01", "", 16, 32 } };
    TimingWarpLibrary warps;
    std::string activePattern = "A01";
    int selected = 0;
    bool playbackEnabled = false, playing = false;
    std::optional<std::size_t> playbackRow, pendingRow;
    SongLaunchQuantization quantization = SongLaunchQuantization::NextSongRow;
    SongLaunchQuantization pendingQuantization = SongLaunchQuantization::NextTick;
    bool loadedLoopTitle = false;
    void setArrangement(const SongArrangement&);
    SongArrangement snapshot() const;
    void setPatterns(std::vector<SongPatternInfo>, std::string active);
    uint32_t patternLength(const std::string&) const;
    uint32_t laneCount(const std::string&) const;
    uint32_t span(std::size_t) const;
    std::string spanSummary(std::size_t) const;
    std::string summary() const;
    std::string queueStatus() const;
    std::vector<SongChoice> choices(std::size_t, SongField) const;
    bool choose(std::size_t, SongField, const SongChoice&);
    bool add();
    bool duplicate();
    bool erase(std::size_t);
    bool move(int offset);
    bool drop(std::size_t source, std::size_t insertion, bool copy);
    bool toggleMute(std::size_t row, uint32_t lane);
    void remapMute(uint32_t source, uint32_t destination, const std::string& pattern);
    bool swing(std::size_t row, std::optional<double> percent);
    double swingPercent(std::size_t row) const;
    bool queueAvailable() const;
    void queue();
    void toggleMode();
    void toggleLoop();

private:
    uint32_t nextIdentity_ = 1;
    std::vector<double> baseSwing_;
    uint32_t identity();
    void removeUnavailableMutes(SongRow&);
    void publish();
};
}
