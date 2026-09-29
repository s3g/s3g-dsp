#pragma once
#include "s3g_ambi_effect_fft.h"
#include "s3g_sample_neon_stack.h"
#include "s3g_sample_neon_family.h"
#include "s3g_sample_player.h"
#include "s3g_sample_wavesets.h"
#include <complex>

namespace s3g::sample {
// Fixed-size, channel-linked frame synthesis. All storage/FFT setup is prepared
// off audio. No spectral atlas, source-length-dependent work or RT allocations.
// Spectral uses phase-continuous STFT resynthesis; Oscillator uses band-limited
// periodic cycle-group tables. Both use the same gesture/stack scheduler.
class NeonFramePlayer {
    static constexpr unsigned N = 1024, H = N / 4, B = N / 2 + 1;
    using Complex = std::complex<float>;
public:
    NeonFramePlayer() = default;
    NeonFramePlayer(const NeonFramePlayer&) = delete;
    NeonFramePlayer& operator=(const NeonFramePlayer&) = delete;
    ~NeonFramePlayer() { unprepare(); }
    bool prepare() {
        unprepare();
        fft_ = ambi_effect_fft::vDSP_create_fftsetup(10, ambi_effect_fft::radix2);
        if (!fft_) return false;
        try {
            analysis_.resize(16 * B); next_.resize(16 * B); sound_.resize(16 * B);
            ring_.resize(16 * N); table_.resize(16 * N); previous_.resize(16 * N);
        } catch (...) { unprepare(); return false; }
        for (unsigned i = 0; i < N; ++i) window_[i] = static_cast<float>(.5 - .5 * std::cos(6.283185307179586 * i / N));
        reset(); return true;
    }
    void unprepare() noexcept {
        if (fft_) ambi_effect_fft::vDSP_destroy_fftsetup(fft_);
        fft_ = nullptr;
        analysis_.clear(); next_.clear(); sound_.clear(); ring_.clear(); table_.clear(); previous_.clear();
    }
    void reset() noexcept {
        initialized_ = false; tick_ = 0; phase_ = 0; cursorCount_ = 0;
        std::fill(ring_.begin(), ring_.end(), 0);
    }
    unsigned cursorCount() const noexcept { return cursorCount_; }
    const auto& cursors() const noexcept { return cursors_; }
    float position() const noexcept { return stackPosition_; }
    void render(const NeonStack* stack, NeonStackLayer fallback, unsigned selected,
        double start, double end, const NeonFamilySettings& family, bool oscillator,
        unsigned group, double sampleRate, float gain, double tune, bool velocity,
        const float* positions, const float* sourcePositions, const float* velocities,
        const uint8_t* triggers, float* const* output, unsigned frames) noexcept {
        cursorCount_ = 0;
        const double pitch = std::pow(2., tune / 12.);
        const double hz = std::min(sampleRate * .2, family[NeonFamily::OscFrequency] * pitch);
        for (unsigned frame = 0; frame < frames; ++frame) {
            if (triggers[frame]) reset();
            if (positions[frame] < 0 || !fft_) {
                if ((positions[frame] == -1 || positions[frame] == -2) && initialized_) reset();
                if (positions[frame] != -1) for (unsigned ch = 0; ch < 16; ++ch) output[ch][frame] = 0;
                continue;
            }
            stackPosition_ = positions[frame];
            const auto blend = neonStackBlend(stackPosition_, stack ? stack->count : 1);
            std::array<NeonStackLayer, 2> sources {{stack ? stack->layers[blend.first] : fallback,
                stack ? stack->layers[blend.second] : fallback}};
            if (blend.first == selected) { sources[0].start = start; sources[0].end = end; }
            if (blend.second == selected) { sources[1].start = start; sources[1].end = end; }
            if (tick_ % H == 0) {
                if (oscillator) makeTable(sources, blend.mix, sourcePositions[frame], group, hz, sampleRate);
                else makeSpectrum(sources, blend.mix, sourcePositions[frame], pitch, sampleRate, family);
                initialized_ = true;
            }
            const float level = gain * (velocity ? velocities[frame] : 1.f);
            const unsigned at = tick_ % N;
            const double point = phase_ * N;
            const auto first = static_cast<unsigned>(point) % N, second = (first + 1) % N;
            const float fraction = static_cast<float>(point - std::floor(point));
            const float join = std::min(1.f, static_cast<float>(tick_ % H + 1) / H);
            for (unsigned ch = 0; ch < 16; ++ch) {
                const unsigned base = ch * N;
                float value;
                if (oscillator) {
                    const float a = previous_[base + first] + fraction * (previous_[base + second] - previous_[base + first]);
                    const float b = table_[base + first] + fraction * (table_[base + second] - table_[base + first]);
                    value = a + join * (b - a);
                } else { value = ring_[base + at]; ring_[base + at] = 0; }
                output[ch][frame] = value * level;
            }
            phase_ += hz / sampleRate; phase_ -= std::floor(phase_);
            ++tick_;
            if (frame + 1 == frames) {
                for (unsigned side = 0; side < 2; ++side) {
                    const auto& source = sources[side];
                    const float weight = side ? blend.mix : 1 - blend.mix;
                    if (!source.asset || weight <= .001f) continue;
                    auto& cursor = cursors_[cursorCount_++];
                    cursor = {static_cast<float>(source.start + sourcePositions[frame] * (source.end - source.start)),
                        60, static_cast<float>(source.start), static_cast<float>(source.end), side + 1u, source.asset};
                    cursor.layer = static_cast<uint8_t>(side ? blend.second : blend.first);
                    cursor.level = weight; cursor.windowPhase = static_cast<float>(phase_);
                }
            }
        }
    }
private:
    bool sameFrame(const std::array<NeonStackLayer, 2>& sources, float mix, double position, double parameter) noexcept {
        bool same = initialized_ && lastMix_ == mix && lastPosition_ == position && lastParameter_ == parameter;
        for (unsigned side = 0; side < 2; ++side) {
            const auto& a = sources[side]; const auto& b = lastSources_[side];
            same &= a.asset == b.asset && a.wavesets == b.wavesets && a.start == b.start && a.end == b.end;
        }
        lastSources_ = sources; lastMix_ = mix; lastPosition_ = position; lastParameter_ = parameter;
        return same;
    }
    static float read(const NeonStackLayer& layer, unsigned channel, double frame) noexcept {
        if (!layer.asset || channel >= layer.asset->channelCount || layer.end <= layer.start) return 0;
        const auto& data = layer.asset->channels[channel];
        if (data.empty()) return 0;
        const double lo = layer.start * (data.size() - 1), hi = layer.end * (data.size() - 1);
        if (frame < lo || frame > hi) return 0;
        const auto i = static_cast<std::size_t>(frame), j = std::min(i + 1, data.size() - 1);
        return data[i] + static_cast<float>(frame - i) * (data[j] - data[i]);
    }
    void forward(const float* samples, Complex* bins) noexcept {
        ambi_effect_fft::DSPSplitComplex split {real_.data(), imag_.data()};
        ambi_effect_fft::vDSP_ctoz(reinterpret_cast<const ambi_effect_fft::DSPComplex*>(samples), 2, &split, 1, N / 2);
        ambi_effect_fft::vDSP_fft_zrip(fft_, &split, 1, 10, ambi_effect_fft::forward);
        bins[0] = {real_[0], 0}; bins[N / 2] = {imag_[0], 0};
        for (unsigned k = 1; k < N / 2; ++k) bins[k] = {real_[k], imag_[k]};
    }
    void inverse(const Complex* bins) noexcept {
        real_[0] = bins[0].real(); imag_[0] = bins[N / 2].real();
        for (unsigned k = 1; k < N / 2; ++k) { real_[k] = bins[k].real(); imag_[k] = bins[k].imag(); }
        ambi_effect_fft::DSPSplitComplex split {real_.data(), imag_.data()};
        ambi_effect_fft::vDSP_fft_zrip(fft_, &split, 1, 10, ambi_effect_fft::inverse);
        ambi_effect_fft::vDSP_ztoc(&split, 1, reinterpret_cast<ambi_effect_fft::DSPComplex*>(frame_.data()), 2, N / 2);
    }
    void makeSpectrum(const std::array<NeonStackLayer, 2>& sources, float mix, double position,
        double pitch, double rate, const NeonFamilySettings& family) noexcept {
        const float blur = family[NeonFamily::SpectralBlur];
        const float smear = family[NeonFamily::SpectralSmear];
        const unsigned channels = std::max(sources[0].asset ? sources[0].asset->channelCount : 0u,
            sources[1].asset ? sources[1].asset->channelCount : 0u);
        const bool cached = sameFrame(sources, mix, position, pitch / rate);
        if (!cached) {
        for (unsigned ch = 0; ch < channels; ++ch) for (unsigned offset = 0; offset < 2; ++offset) {
            for (unsigned i = 0; i < N; ++i) {
                float sample = 0;
                for (unsigned side = 0; side < 2; ++side) {
                    const auto& s = sources[side]; if (!s.asset) continue;
                    const double lo = s.start * (s.asset->frameCount() - 1), hi = s.end * (s.asset->frameCount() - 1);
                    const double step = s.asset->sampleRate / rate * pitch;
                    const double begin = lo + position * std::max(0., hi - lo - (N + H - 1) * step);
                    sample += (side ? mix : 1 - mix) * read(s, ch, std::clamp(begin + (i + offset * H) * step, lo, std::max(lo, hi)));
                }
                frame_[i] = sample * window_[i];
            }
            float mean = 0;
            for (float value : frame_) mean += value / (N * .5f);
            for (unsigned i = 0; i < N; ++i) frame_[i] -= mean * window_[i];
            forward(frame_.data(), (offset ? next_ : analysis_).data() + ch * B);
        }
        for (unsigned k = 1; k < N / 2; ++k) {
            Complex advance {};
            for (unsigned ch = 0; ch < channels; ++ch) advance += std::conj(analysis_[ch * B + k]) * next_[ch * B + k];
            if (std::abs(advance) > 1.e-16f) rotations_[k] = advance / std::abs(advance);
            else if (!initialized_ || smear == 0) rotations_[k] = Complex(1, 0);
            // A silent target has no new phase estimate. Retain the old
            // rotation for the Smear tail instead of detuning it to the grid.
        }
        }
        const float follow = std::min(1 - .985f * blur * blur, smear > 0
            ? static_cast<float>(-std::expm1(-double(H) / (rate * smear))) : 1.f);
        // Smooth a joint energy envelope, then apply its gain to every channel
        // identically. Blur is audible even on a frozen frame without mixing
        // ACN components or assigning independent channel phases.
        std::array<double, B + 1> energySum {};
        for (unsigned k = 0; k < B; ++k) {
            double energy = 0;
            for (unsigned ch = 0; ch < channels; ++ch) energy += std::norm(analysis_[ch * B + k]);
            energySum[k + 1] = energySum[k] + energy;
        }
        const unsigned radius = 1 + static_cast<unsigned>(blur * 24);
        // Colour is separate from frame evolution: long Smear must not make
        // Tilt/Thin gestures take seconds to respond. All masks are joint-field
        // gains; never normalize or randomize ACN components independently.
        const float focus = family[NeonFamily::SpectralFocus];
        const float tilt = family[NeonFamily::SpectralTilt];
        const float thin = family[NeonFamily::SpectralThin];
        const double total = energySum[N / 2] - energySum[1];
        const double average = std::max(1.e-16, total / (N / 2 - 1));
        std::array<float, B> colour {};
        double focusedEnergy = 0;
        for (unsigned k = 1; k < N / 2; ++k) {
            const double energy = energySum[k + 1] - energySum[k];
            colour[k] = focus == 0 ? 1.f : static_cast<float>(std::clamp(
                std::pow(std::max(1.e-12, energy / average), .5 * focus), .0625, 8.));
            focusedEnergy += energy * colour[k] * colour[k];
        }
        // Focus changes contrast, not total analysed energy. Never lift silence
        // and cap individual gains; Tilt may add at most 12 dB, Thin only cuts.
        const float focusLevel = focus == 0 || total <= 1.e-16 ? 1.f
            : static_cast<float>(std::clamp(std::sqrt(total / std::max(1.e-16, focusedEnergy)), .125, 8.));
        const float colourFollow = static_cast<float>(-std::expm1(-double(H) / (rate * .012)));
        for (unsigned k = 1; k < N / 2; ++k) {
            float gain = std::min(8.f, colour[k] * focusLevel);
            if (tilt != 0) gain *= static_cast<float>(std::pow(10., std::clamp(
                tilt * std::log2(k * rate / (N * 1000.)), -24., 12.) / 20.));
            if (thin > 0) {
                // Stable three-bin islands, not moving random gates. Increasing
                // Thin removes nested bands; all pads/channels use the same mask.
                uint32_t hash = (k / 3 + 1) * 0x9e3779b9u;
                hash ^= hash >> 16; hash *= 0x85ebca6bu; hash ^= hash >> 13;
                const float rank = static_cast<float>(hash >> 8) / 16777216.f;
                const float edge = std::clamp((rank + .06f - thin * 1.04f) / .06f, 0.f, 1.f);
                gain *= edge * edge * (3 - 2 * edge);
            }
            colourGains_[k] = initialized_ ? colourGains_[k] + colourFollow * (gain - colourGains_[k]) : gain;
            Complex align {};
            for (unsigned ch = 0; ch < channels; ++ch) {
                const auto i = ch * B + k;
                align += std::conj(analysis_[i]) * sound_[i];
            }
            const Complex alignment = initialized_ && std::abs(align) > 1.e-16f ? align / std::abs(align) : Complex(1, 0);
            const unsigned lo = k > radius ? k - radius : 1, hi = std::min(N / 2, k + radius + 1);
            const double mean = (energySum[hi] - energySum[lo]) / (hi - lo);
            const float spread = 1 + blur * (static_cast<float>(std::min(8., std::sqrt(mean / std::max(1.e-16, energySum[k + 1] - energySum[k])))) - 1);
            for (unsigned ch = 0; ch < channels; ++ch) {
                const auto i = ch * B + k;
                // One complex multiplier per bin for the entire channel field.
                const auto target = analysis_[i] * spread;
                sound_[i] = initialized_ ? sound_[i] + follow * (target * alignment - sound_[i]) : target;
                next_[i] = sound_[i] * colourGains_[k]; sound_[i] *= rotations_[k];
            }
        }
        for (unsigned ch = 0; ch < channels; ++ch) {
            next_[ch * B] = next_[ch * B + N / 2] = {}; // suppress DC/Nyquist
            inverse(next_.data() + ch * B);
            for (unsigned i = 0; i < N; ++i)
                ring_[ch * N + (tick_ + i) % N] += frame_[i] * window_[i] / (3.f * N);
        }
    }
    void makeTable(const std::array<NeonStackLayer, 2>& sources, float mix, double position,
        unsigned group, double hz, double rate) noexcept {
        std::copy(table_.begin(), table_.end(), previous_.begin());
        if (sameFrame(sources, mix, position, hz / rate) && group == lastGroup_) return;
        lastGroup_ = group;
        std::fill(table_.begin(), table_.end(), 0);
        std::array<double, 2> begins {}, lengths {};
        for (unsigned side = 0; side < 2; ++side) {
            const auto& s = sources[side];
            if (!s.asset || !s.wavesets || s.wavesets->asset.get() != s.asset) continue;
            const auto& units = s.wavesets->channelUnits[0];
            const double lo = s.start * (s.asset->frameCount() - 1), hi = s.end * (s.asset->frameCount() - 1);
            const auto first = std::lower_bound(units.begin(), units.end(), lo,
                [](const WavesetUnit& u, double p) { return u.startPosition < p; });
            const auto last = std::upper_bound(first, units.end(), hi,
                [](double p, const WavesetUnit& u) { return p < u.endPosition; });
            const auto count = static_cast<unsigned>(last - first);
            if (!count) continue;
            const auto n = std::min(count - 1, static_cast<unsigned>(position * count));
            begins[side] = first[n].startPosition;
            lengths[side] = first[std::min(count - 1, n + group - 1)].endPosition - begins[side];
        }
        const unsigned limit = std::min(N / 2 - 1, static_cast<unsigned>(rate * .45 / hz));
        const unsigned channels = std::max(sources[0].asset ? sources[0].asset->channelCount : 0u,
            sources[1].asset ? sources[1].asset->channelCount : 0u);
        for (unsigned ch = 0; ch < channels; ++ch) {
            float mean = 0;
            for (unsigned i = 0; i < N; ++i) {
                float value = 0;
                for (unsigned side = 0; side < 2; ++side) if (lengths[side] > 0) {
                    const auto& s = sources[side];
                    const double t = static_cast<double>(i) / N;
                    // Remove the endpoint mismatch with the same ramp on every
                    // channel, then band-limit the periodic table before use.
                    const float seam = read(s, ch, begins[side] + lengths[side]) - read(s, ch, begins[side]);
                    value += (side ? mix : 1 - mix) * (read(s, ch, begins[side] + t * lengths[side]) - static_cast<float>(t) * seam);
                }
                frame_[i] = value; mean += value / N;
            }
            for (auto& value : frame_) value -= mean;
            forward(frame_.data(), analysis_.data());
            analysis_[0] = {};
            for (unsigned k = limit + 1; k < B; ++k) analysis_[k] = {};
            inverse(analysis_.data());
            for (unsigned i = 0; i < N; ++i) table_[ch * N + i] = frame_[i] / (2.f * N);
        }
        if (!initialized_) std::copy(table_.begin(), table_.end(), previous_.begin());
    }
    ambi_effect_fft::FFTSetup fft_ = nullptr;
    std::array<float, N> window_ {}, frame_ {};
    std::array<float, N / 2> real_ {}, imag_ {};
    std::array<Complex, B> rotations_ {};
    std::array<float, B> colourGains_ {};
    std::array<NeonStackLayer, 2> lastSources_ {};
    float lastMix_ = -1;
    double lastPosition_ = -1, lastParameter_ = -1;
    unsigned lastGroup_ = 0;
    std::vector<Complex> analysis_, next_, sound_;
    std::vector<float> ring_, table_, previous_;
    std::array<VoiceCursor, kMaximumVoices> cursors_ {};
    unsigned cursorCount_ = 0;
    uint64_t tick_ = 0;
    double phase_ = 0;
    float stackPosition_ = 0;
    bool initialized_ = false;
};
}
