#include "s3g/tracker/preview_sequencer.h"
#include <algorithm>
#include <cmath>

namespace s3g::tracker {
void PreviewSequencer::reclaim() {
    const auto acknowledged = acknowledged_.load(std::memory_order_acquire);
    plans_.erase(std::remove_if(plans_.begin(), plans_.end(),
        [acknowledged](const auto& p) { return p->token < acknowledged; }),
        plans_.end());
}
uint32_t PreviewSequencer::publish(std::vector<PitchPreviewEvent> events,
    uint8_t channel, double bpm, uint32_t ticks, uint32_t rows, bool loop,
    bool followHostTempo) {
    reclaim();
    auto p = std::make_unique<Plan>();
    p->token = ++requested_;
    p->channel = std::clamp<uint8_t>(channel, 1, 16);
    p->bpm = std::clamp(std::isfinite(bpm) ? bpm : 120., 1., 1000.);
    p->ticks = std::clamp(ticks, 1u, 96u);
    p->followHostTempo = followHostTempo;
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a.row != b.row ? a.row < b.row : a.position < b.position;
    });
    p->rows = rows ? rows : events.empty() ? 0 : events.back().row + 1;
    events.erase(std::remove_if(events.begin(), events.end(),
        [&](const auto& e) { return e.row >= p->rows; }), events.end());
    p->events = std::move(events);
    const auto* raw = p.get();
    plans_.push_back(std::move(p));
    loopingToken_.store(loop ? requested_ : 0, std::memory_order_release);
    pending_.store(raw, std::memory_order_release);
    return requested_;
}
void PreviewSequencer::cancel(uint32_t token) {
    if (token == 0 || token == requested_)
        publish({}, 1, 120, 4);
}
void PreviewSequencer::setLoop(uint32_t token, bool enabled) {
    if (token == requested_)
        loopingToken_.store(enabled ? token : 0, std::memory_order_release);
}
int64_t PreviewSequencer::position(uint32_t token) {
    reclaim();
    if (!token || token != requested_) return -1;
    const auto snapshot = progress_.load(std::memory_order_acquire);
    if (uint32_t(snapshot >> 32) != token) return 0; // queued, not started
    return int64_t(uint32_t(snapshot)) - 1;
}
void PreviewSequencer::report(int64_t row) noexcept {
    progress_.store((uint64_t(consumed_) << 32) | uint32_t(row + 1),
        std::memory_order_release);
}
void PreviewSequencer::reset() noexcept {
    const auto* p = pending_.load(std::memory_order_acquire);
    consumed_ = p ? p->token : 0;
    active_ = nullptr;
    acknowledged_.store(consumed_, std::memory_order_release);
    report(-1);
}
bool PreviewSequencer::beginBlock(uint32_t frames, double sampleRate,
    double hostBpm, double tempoIncrement) noexcept {
    const auto* p = pending_.load(std::memory_order_acquire);
    const bool changed = p && p->token != consumed_;
    if (changed) {
        consumed_ = p->token;
        active_ = p->events.empty() || !p->rows ? nullptr : p;
        row_ = 0;
        cycle_ = 0;
        event_ = 0;
        acknowledged_.store(consumed_, std::memory_order_release);
        report(active_ ? 0 : -1);
    }
    frames_ = frames;
    if (!active_) return changed;
    const bool loop = loopingToken_.load(std::memory_order_acquire) == consumed_;
    if (!loop && (changed || looping_))
        endRow_ = (std::floor(row_ / active_->rows) + 1) * active_->rows;
    looping_ = loop;
    const bool hostTempo = active_->followHostTempo &&
        std::isfinite(hostBpm) && hostBpm > 0;
    const auto bpm = hostTempo ? hostBpm : active_->bpm;
    const auto scale = static_cast<long double>(active_->ticks) /
        (60.L * sampleRate);
    step_ = std::max(1., bpm) * scale;
    acceleration_ = hostTempo && std::isfinite(tempoIncrement)
        ? tempoIncrement * scale : 0;
    // Invalid ramps must not reverse musical time.
    if (step_ + acceleration_ * frames < scale) acceleration_ = 0;
    return changed;
}
long double PreviewSequencer::rowAt(uint32_t offset) const noexcept {
    return row_ + step_ * offset + acceleration_ * offset *
        (static_cast<long double>(offset) - 1) / 2;
}
bool PreviewSequencer::next(PreviewHit& hit) noexcept {
    if (!active_ || frames_ == 0) return false;
    if (event_ == active_->events.size()) {
        if (!looping_) return false;
        event_ = 0;
        ++cycle_;
    }
    const auto& event = active_->events[event_];
    const long double target = static_cast<long double>(cycle_) * active_->rows
        + event.row + static_cast<long double>(event.position) / 65536;
    if (!looping_ && target >= endRow_) return false;
    // Lower-bound on the sample clock, with a tiny tolerance for arithmetic
    // noise at exact boundaries. Fractional row/loop lengths are never rounded
    // and re-used, preventing cumulative drift at non-integral BPMs.
    constexpr long double epsilon = 1e-10L;
    if (target > rowAt(frames_ - 1) + epsilon) return false;
    uint32_t low = 0, high = frames_ - 1;
    while (low < high) {
        const auto mid = low + (high - low) / 2;
        if (rowAt(mid) + epsilon < target) low = mid + 1;
        else high = mid;
    }
    hit.event = event;
    hit.offset = low;
    hit.channel = active_->channel;
    hit.duration = static_cast<uint64_t>(std::max(1.L, std::round(
        event.gatePercent / (100.L * (step_ + acceleration_ * low)))));
    ++event_;
    return true;
}
void PreviewSequencer::endBlock() noexcept {
    if (!active_) return;
    row_ = rowAt(frames_);
    // A sub-row onset immediately before the end can quantize to the first
    // sample of the next block. Keep it until emitted, even for a one-shot.
    const bool finalHitPending = event_ < active_->events.size() &&
        static_cast<long double>(cycle_) * active_->rows +
            active_->events[event_].row +
            static_cast<long double>(active_->events[event_].position) / 65536 < endRow_;
    if (!looping_ && !finalHitPending && row_ + 1e-10L >= endRow_) {
        active_ = nullptr;
        report(-1);
    } else {
        report(static_cast<int64_t>(std::fmod(row_,
            static_cast<long double>(active_->rows))));
    }
}
} // namespace s3g::tracker
