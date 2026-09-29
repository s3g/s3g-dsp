#pragma once
#include "s3g_sample_neon.h"

namespace s3g::sample {
// A bounded pool of complete playback gestures, not a pool of grains. A
// gesture owns its scheduler, envelopes, readers, FFT and stack-path phase.
// The existing renderer is reused unchanged for each method. Character FX are
// applied once to the sum for a pad, so echo tails are not stolen with a note.
class SampleNeonPolyEngine {
public:
    static constexpr unsigned capacity = 32;
    using MotionVisual = SampleNeonEngine::MotionVisual;
    bool prepare(double rate, uint32_t frames) {
        unprepare(); rate_ = rate; maximum_ = frames;
        if (!core_.prepare(rate, frames, false)) return false;
        try {
            for (auto& b : padAudio_) b.resize(16u * frames);
            for (auto& b : dummy_) b.resize(frames);
        } catch (...) { unprepare(); return false; }
        for (auto& fx : effects_) if (!fx.prepare(rate)) { unprepare(); return false; }
        reset(); return true;
    }
    void unprepare() noexcept {
        core_.unprepare(); for (auto& fx : effects_) fx.unprepare();
        for (auto& b : padAudio_) b.clear(); for (auto& b : dummy_) b.clear();
        maximum_ = 0; voices_ = {}; held_ = {};
    }
    void reset() noexcept {
        core_.reset(); for (auto& fx : effects_) fx.reset();
        voices_ = {}; held_ = {}; pressure_.fill(0); peaks_.fill(0);
        cursorCounts_.fill(0); latest_.fill(-1); serial_ = 0; peak_ = 0;
        for (unsigned p = 0; p < 32; ++p) { routing_[p].reset(0x6d2b79f5u+p); cutRouting_[p].reset(cutSeeds_[p]); }
    }
    void killAll() noexcept { reset(); }
    void setPreparedAsset(std::size_t pad, const SampleAsset* asset) noexcept {
        if (pad >= 32 || assets_[pad] == asset) return;
        stopPad(static_cast<unsigned>(pad)); assets_[pad] = asset;
    }
    bool setAsset(std::size_t pad, const SampleAsset* asset) noexcept {
        if (pad >= 32 || (asset && !sampleNeonChannelCountSupported(asset->channelCount))) return false;
        setPreparedAsset(pad, asset); return true;
    }
    void setPreparedWavesets(std::size_t pad, const WavesetMap* map) noexcept { if (pad < 32) maps_[pad] = map; }
    bool slotLoaded(std::size_t pad) const noexcept { return pad < 32 && assets_[pad]; }
    float slotPeak(std::size_t pad) const noexcept { return peaks_[pad]; }
    float slotPressure(std::size_t pad) const noexcept { return pressure_[pad]; }
    float outputPeak() const noexcept { return peak_; }
    std::size_t activeVoiceCount() const noexcept { return core_.activeVoiceCount(); }
    unsigned activeNoteCount(unsigned pad) const noexcept {
        unsigned n = 0; for (const auto& v : voices_) n += v.used && v.pad == pad; return n;
    }
    bool slotPlaybackActive(std::size_t pad) const noexcept {
        for (unsigned i = 0; i < capacity; ++i) if (voices_[i].used && voices_[i].pad == pad && core_.slotPlaybackActive(i)) return true;
        return false;
    }
    float stackPosition(std::size_t pad) const noexcept { const int v = latest_[pad]; return v < 0 ? 0 : core_.stackPosition(v); }
    float stackPathPhase(std::size_t pad, const SampleNeonSettings&) const noexcept {
        const int v = latest_[pad]; return v < 0 ? -1 : core_.stackPathPhase(v, renderSettings_);
    }
    int stackWaveformLayer(std::size_t pad, const SampleNeonSettings&) const noexcept {
        const int v = latest_[pad]; return v < 0 ? -1 : core_.stackWaveformLayer(v, renderSettings_);
    }
    bool stackScanActive(std::size_t pad, const SampleNeonSettings&) const noexcept {
        const int v = latest_[pad]; return v >= 0 && core_.stackScanActive(v, renderSettings_);
    }
    double motionPosition(std::size_t pad, const SampleNeonSettings&) const noexcept {
        const int v = latest_[pad]; return v < 0 ? 0 : core_.motionPosition(v, renderSettings_);
    }
    MotionVisual motionVisual(std::size_t pad, const SampleNeonSettings&) const noexcept {
        const int v = latest_[pad]; return v < 0 ? MotionVisual {} : core_.motionVisual(v, renderSettings_);
    }
    const auto& voiceCursors(std::size_t pad) const noexcept { return cursors_[pad]; }
    uint32_t voiceCursorCount(std::size_t pad) const noexcept { return cursorCounts_[pad]; }

    void render(const SampleNeonSettings& settings, const SampleNeonEvent* events,
        std::size_t count, float* const* output, uint32_t channels, uint32_t frames) noexcept {
        if (!output || (channels != 2 && channels != kSampleNeonOutputChannels)) return;
        for (unsigned ch = 0; ch < channels; ++ch) { if (!output[ch]) return; std::fill_n(output[ch], frames, 0.f); }
        if (!maximum_ || frames > maximum_) return;
        peaks_.fill(0); peak_ = 0;
        count = events ? std::min(count, kSampleNeonMaximumBlockEvents) : 0;
        // Changes in voice policy stop that pad; a live limit reduction cannot
        // leave inaccessible held voices behind. Audio/FX routing stays per pad.
        for (unsigned p = 0; p < 32; ++p) {
            const auto& s = settings.slots[p];
            const unsigned policy = s.noteVoiceMode | (unsigned(s.noteVoiceLimit) << 8)
                | (unsigned(s.playback) << 16);
            if (policies_[p] != policy) { stopPad(p); policies_[p] = policy; }
            const auto seed = static_cast<uint32_t>(s.family[NeonFamily::CutSeed]);
            if (seed != cutSeeds_[p]) { cutSeeds_[p] = seed; cutRouting_[p].reset(seed); }
        }
        uint32_t begin = 0; std::size_t next = 0;
        while (begin < frames) {
            pendingCount_ = 0;
            while (next < count && events[next].frameOffset <= begin) handle(settings, events[next++]);
            const uint32_t end = next < count ? std::min(frames, std::max(begin + 1, events[next].frameOffset)) : frames;
            renderRange(settings, output, channels, begin, end - begin);
            begin = end;
        }
        publishVisuals();
    }
private:
    struct Voice {
        bool used = false, released = false, legacy = false;
        uint8_t pad = 0, key = 255;
        uint64_t external = 0, identity = 0, age = 0;
        uint8_t ordinal = 0;
    };
    struct Held { bool used = false; uint8_t pad = 0, key = 255; uint64_t id = 0, age = 0; };
    void stopVoice(unsigned i) noexcept {
        core_.resetVoice(i); voices_[i] = {};
        // Several note-ons may share a timestamp and steal the same renderer.
        for (std::size_t n = 0; n < pendingCount_;) {
            if (pending_[n].slot != i) { ++n; continue; }
            for (std::size_t j = n + 1; j < pendingCount_; ++j) pending_[j - 1] = pending_[j];
            --pendingCount_;
        }
    }
    void stopPad(unsigned pad) noexcept {
        for (unsigned i = 0; i < capacity; ++i) if (voices_[i].used && voices_[i].pad == pad) stopVoice(i);
        for (auto& h : held_) if (h.used && h.pad == pad) h.used = false;
        effects_[pad].reset(); latest_[pad] = -1;
        routing_[pad].reset(0x6d2b79f5u+pad);
        cutRouting_[pad].reset(cutSeeds_[pad]);
    }
    static unsigned mode(const SampleNeonSlotSettings& s, uint8_t key) noexcept {
        // Existing projects retain original engine behavior. New chromatic
        // addressing defaults to Poly; choosing Mono/Legato overrides it.
        return s.noteVoiceMode ? s.noteVoiceMode : key < 128 ? 2u : 0u;
    }
    void queue(unsigned index, SampleNeonEvent e) noexcept {
        if (pendingCount_ == pending_.size()) return;
        e.frameOffset = 0; e.slot = static_cast<uint8_t>(index);
        if (!voices_[index].legacy && e.kind != SampleNeonEventKind::StackPosition) e.noteId = voices_[index].identity;
        pending_[pendingCount_++] = e;
    }
    void remember(const SampleNeonEvent& e) noexcept {
        Held* entry = nullptr;
        for (auto& h : held_) if (h.used && h.pad == e.slot && h.id == e.noteId) { entry = &h; break; }
        if (!entry) for (auto& h : held_) if (!h.used) { entry = &h; break; }
        if (!entry) entry = &*std::min_element(held_.begin(), held_.end(), [](auto& a, auto& b) { return a.age < b.age; });
        *entry = {true, e.slot, e.key, e.noteId, ++serial_};
    }
    void handle(const SampleNeonSettings& settings, const SampleNeonEvent& e) noexcept {
        if (e.slot >= 32) return;
        const auto& s = settings.slots[e.slot];
        if (e.kind == SampleNeonEventKind::Choke && e.noteId == 0) { stopPad(e.slot); return; }
        if (e.kind == SampleNeonEventKind::Pressure) pressure_[e.slot] = std::clamp(e.value, 0.f, 1.f);
        if (e.kind != SampleNeonEventKind::Trigger) {
            if (e.kind == SampleNeonEventKind::Release || e.kind == SampleNeonEventKind::Choke)
                for (auto& h : held_) if (h.used && h.pad == e.slot && h.id == e.noteId) h.used = false;
            for (unsigned i = 0; i < capacity; ++i) {
                auto& v = voices_[i]; if (!v.used || v.pad != e.slot) continue;
                const bool control = e.kind == SampleNeonEventKind::StackPosition || (e.kind == SampleNeonEventKind::Pressure && !e.noteId);
                if (!control && !v.legacy && v.external != e.noteId) continue;
                if (e.kind == SampleNeonEventKind::Release && mode(s, v.key) == 3) {
                    const Held* last = nullptr;
                    for (const auto& h : held_) if (h.used && h.pad == e.slot && (!last || h.age > last->age)) last = &h;
                    if (last) { v.external = last->id; v.key = last->key; continue; }
                }
                if (e.kind == SampleNeonEventKind::Choke && !v.legacy) { stopVoice(i); continue; }
                queue(i, e);
                if (e.kind == SampleNeonEventKind::Release) v.released = true;
            }
            return;
        }
        if (s.chokeGroup) for (unsigned p = 0; p < 32; ++p)
            if (p != e.slot && settings.slots[p].chokeGroup == s.chokeGroup) stopPad(p);
        const bool audition = e.mode != controller::reloop_neon::Mode::Sampler || e.selectedSource;
        const unsigned policy = audition ? 1u : mode(s, e.key);
        if (policy == 3) remember(e);
        int existing = -1; unsigned inPad = 0;
        for (unsigned i = 0; i < capacity; ++i) if (voices_[i].used && voices_[i].pad == e.slot) {
            ++inPad; if (existing < 0 || voices_[i].age > voices_[existing].age) existing = i;
        }
        if (policy == 3 && existing >= 0 && !voices_[existing].released && core_.slotPlaybackActive(existing)) {
            auto& v = voices_[existing]; v.external = e.noteId; v.key = e.key; v.age = ++serial_; return;
        }
        // Toggle releases the matching key only. Other pitches keep sounding.
        if (s.triggerMode == TriggerMode::Toggle) for (unsigned i = 0; i < capacity; ++i) {
            auto& v = voices_[i];
            if (v.used && v.pad == e.slot && v.key == e.key && !v.released) { queue(i, e); v.released = true; return; }
        }
        if (policy == 0 && existing >= 0 && voices_[existing].legacy) {
            voices_[existing].released = false; queue(existing, e); return;
        }
        const unsigned limit = policy == 2 ? std::clamp<unsigned>(s.noteVoiceLimit, 1, 16) : 1;
        int chosen = -1;
        if (inPad >= limit) {
            for (unsigned i = 0; i < capacity; ++i) if (voices_[i].used && voices_[i].pad == e.slot
                && (chosen < 0 || older(voices_[i], voices_[chosen]))) chosen = i;
        } else if (!voices_[e.slot].used) chosen = e.slot;
        else for (unsigned i = 0; i < capacity; ++i) if (!voices_[i].used) { chosen = i; break; }
        if (chosen < 0) for (unsigned i = 0; i < capacity; ++i)
            if (chosen < 0 || older(voices_[i], voices_[chosen])) chosen = i;
        if (policy != 2) for (unsigned i = 0; i < capacity; ++i)
            if (voices_[i].used && voices_[i].pad == e.slot) stopVoice(i);
        stopVoice(chosen);
        auto& v = voices_[chosen];
        v = {true, false, policy == 0, e.slot, e.key, e.noteId, ++serial_, serial_};
        uint32_t ordinals = 0;
        for (unsigned i = 0; i < capacity; ++i) if (i != static_cast<unsigned>(chosen) && voices_[i].used && voices_[i].pad == e.slot)
            ordinals |= uint32_t(1) << voices_[i].ordinal;
        while (v.ordinal < 31 && (ordinals & (uint32_t(1) << v.ordinal))) ++v.ordinal;
        core_.setPreparedAsset(chosen, assets_[e.slot]); core_.setPreparedWavesets(chosen, maps_[e.slot]);
        queue(chosen, e);
        // Reapply pad pressure to this new renderer without touching siblings.
        SampleNeonEvent pressure; pressure.kind = SampleNeonEventKind::Pressure; pressure.value = pressure_[e.slot];
        queue(chosen, pressure);
    }
    static bool older(const Voice& a, const Voice& b) noexcept {
        return a.released != b.released ? a.released : a.age < b.age;
    }
    static void sumVoice(void* context, unsigned index, float* const* audio, unsigned channels, uint32_t frames) {
        auto& p = *static_cast<SampleNeonPolyEngine*>(context);
        auto& target = p.padAudio_[p.voices_[index].pad];
        for (unsigned ch = 0; ch < channels; ++ch) for (unsigned f = 0; f < frames; ++f)
            target[ch * p.maximum_ + f] += audio[ch][f];
    }
    void renderRange(const SampleNeonSettings& settings, float* const* output, unsigned channels, unsigned offset, unsigned frames) noexcept {
        renderSettings_ = settings;
        if (renderSettings_.hostBeatValid && settings.transportPlaying)
            renderSettings_.hostBeatPosition += offset / rate_ * settings.hostTempoBpm / 60.;
        uint32_t mask = 0;
        for (unsigned i = 0; i < capacity; ++i) if (voices_[i].used) {
            mask |= uint32_t(1) << i;
            const auto& v = voices_[i]; auto& s = renderSettings_.slots[i]; s = settings.slots[v.pad];
            s.noteRoutingAllocator = &routing_[v.pad];
            s.noteCutRoutingAllocator = v.legacy ? nullptr : &cutRouting_[v.pad];
            s.noteVoiceOrdinal = v.legacy ? 255 : v.ordinal;
            s.chokeGroup = 0; // Managed once by the pad allocator, not virtual slot numbers.
            const double pitch = v.key < 128 ? int(v.key) - int(s.rootNote) : 0;
            s.tuneSemitones = static_cast<float>(std::clamp(s.tuneSemitones + pitch, -60., 60.));
            if (s.playback == SampleNeonPlayback::Motion) {
                const double ratio = std::pow(2., pitch / 12.);
                s.motionCycleSeconds /= ratio; s.motionCycleBeats /= ratio;
            }
            core_.setPreparedWavesets(i, maps_[v.pad]);
        }
        for (auto& b : padAudio_) for (unsigned ch = 0; ch < 16; ++ch) std::fill_n(b.data() + ch * maximum_, frames, 0.f);
        std::array<float*, kSampleNeonOutputChannels> dummy;
        for (unsigned ch = 0; ch < dummy.size(); ++ch) dummy[ch] = dummy_[ch].data();
        core_.render(renderSettings_, pending_.data(), pendingCount_, dummy.data(), kSampleNeonOutputChannels, frames, sumVoice, this, mask);
        const float master = std::pow(10.f, std::clamp(settings.masterGainDecibels, -60.f, 12.f) * .05f);
        for (unsigned p = 0; p < 32; ++p) {
            const auto& s = settings.slots[p]; const auto* asset = assets_[p];
            const bool distribute = neonDistributes(s);
            if (!asset || !sampleNeonRouteCompatible(asset->channelCount, s.sourceFormat, settings.outputLayout, s.outputBus, distribute)) continue;
            const unsigned width = distribute ? sampleNeonBusWidth(settings.outputLayout) : std::max<unsigned>(2u, asset->channelCount);
            const unsigned first = s.outputBus * sampleNeonBusWidth(settings.outputLayout);
            if (first + width > channels) continue;
            const unsigned processed = distribute ? width : asset->channelCount;
            std::array<float*, 16> data; for (unsigned ch = 0; ch < 16; ++ch) data[ch] = padAudio_[p].data() + ch * maximum_;
            effects_[p].process(data.data(), processed, frames, s.character, s.fx,
                std::clamp(settings.globalMangle + s.mangle + pressure_[p] * s.pressureDepth, 0.f, 1.f),
                s.sourceFormat == SampleNeonSourceFormat::Ambisonic, settings.hostTempoBpm);
            for (unsigned ch = 0; ch < width; ++ch) {
                const float balance = (distribute ? width : asset->channelCount) > 2 ? 1.f
                    : std::sqrt(std::min(1.f, ch == 0 ? 1 - std::clamp(s.pan, -1.f, 1.f) : 1 + std::clamp(s.pan, -1.f, 1.f)));
                for (unsigned f = 0; f < frames; ++f) {
                    const float sample = data[!distribute && asset->channelCount == 1 ? 0 : ch][f] * balance;
                    peaks_[p] = std::max(peaks_[p], std::abs(sample));
                    auto& out = output[first + ch][offset + f]; out += sample * master; peak_ = std::max(peak_, std::abs(out));
                }
            }
        }
        for (unsigned i = 0; i < capacity; ++i) if (voices_[i].used && !core_.slotPlaybackActive(i)) stopVoice(i);
    }
    void publishVisuals() noexcept {
        latest_.fill(-1); cursorCounts_.fill(0);
        for (unsigned i = 0; i < capacity; ++i) if (voices_[i].used) {
            const auto& v = voices_[i]; auto& last = latest_[v.pad];
            if (last < 0 || v.age > voices_[last].age) last = i;
            auto& count = cursorCounts_[v.pad];
            for (unsigned c = 0; c < core_.voiceCursorCount(i) && count < kMaximumVoices; ++c) {
                auto cursor = core_.voiceCursors(i)[c];
                cursor.noteId ^= v.identity << 32;
                if (v.key < 128) cursor.key = v.key;
                cursors_[v.pad][count++] = cursor;
            }
        }
    }
    SampleNeonEngine core_;
    SampleNeonSettings renderSettings_;
    std::array<Voice, capacity> voices_ {};
    std::array<Held, 256> held_ {};
    std::array<SampleNeonFx, 32> effects_;
    std::array<s3g::routing::TriggerOutputAllocator<16u>,32> routing_;
    std::array<s3g::routing::TriggerOutputAllocator<32u>,32> cutRouting_;
    std::array<const SampleAsset*, 32> assets_ {};
    std::array<const WavesetMap*, 32> maps_ {};
    std::array<std::vector<float>, 32> padAudio_;
    std::array<std::vector<float>, kSampleNeonOutputChannels> dummy_;
    std::array<float, 32> pressure_ {}, peaks_ {};
    std::array<unsigned, 32> policies_ {}, cursorCounts_ {};
    std::array<uint32_t,32> cutSeeds_ {};
    std::array<int, 32> latest_ {};
    std::array<std::array<VoiceCursor, kMaximumVoices>, 32> cursors_ {};
    std::array<SampleNeonEvent, kSampleNeonMaximumBlockEvents> pending_ {};
    std::size_t pendingCount_ = 0;
    uint64_t serial_ = 0;
    double rate_ = 48000;
    uint32_t maximum_ = 0;
    float peak_ = 0;
};
} // namespace s3g::sample
