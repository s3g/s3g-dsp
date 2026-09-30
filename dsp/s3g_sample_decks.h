#pragma once

#include "s3g_sample_neon_poly.h"
#include "s3g_sample_decks_roll.h"
#include "s3g_sample_decks_transitions.h"
#include <array>
#include <cmath>
#include <vector>

namespace s3g::sample {

// Two logical decks share Neon's bounded polyphonic gesture pool, with
// separate pre-crossfader buses. Reusing the actual engine keeps all nine
// methods, their envelopes, note ownership and spatial rules.
enum class DeckFadeCurve : uint8_t { Linear, EqualPower, Cut };
inline std::array<float, 2> deckFadeGains(float position, DeckFadeCurve curve) noexcept {
    const float x = std::clamp(position, 0.f, 1.f);
    if (curve == DeckFadeCurve::Cut) return {std::min(1.f, (1-x)*64), std::min(1.f, x*64)};
    if (curve == DeckFadeCurve::EqualPower) return {std::cos(x*1.5707963267949f), std::sin(x*1.5707963267949f)};
    return {1-x, x};
}
inline SampleNeonOutputLayout decksLayout(unsigned format) noexcept {
    constexpr SampleNeonOutputLayout layouts[] {SampleNeonOutputLayout::StereoStems,
        SampleNeonOutputLayout::Quad, SampleNeonOutputLayout::Octo,
        SampleNeonOutputLayout::Ambisonic1, SampleNeonOutputLayout::Ambisonic2,
        SampleNeonOutputLayout::Ambisonic3};
    return layouts[std::min(format, 5u)];
}

struct SampleDecksMix {
    float crossfade = .5f, gainDecibels = -6.f;
    std::array<float, 2> levels {{1,1}};
    std::array<float, 2> trimDb {}, lowDb {}, midDb {}, highDb {}, filter {};
    DeckFadeCurve curve = DeckFadeCurve::Linear;
    bool stems = false; // main mix first, optional A/B buses follow if they fit
};

// A damped platter driven by relative angular displacement, measured in source
// seconds rather than a percentage of the file. Both filters run on the audio
// clock: sparse MIDI packets and host block size cannot produce position steps.
class DeckPlatter {
public:
    void prepare(double rate) noexcept {
        rate_ = rate;
        handAlpha_ = 1-std::exp(-1/(.003*rate));
        brakeAlpha_ = 1-std::exp(-1/(.006*rate));
        motorAlpha_ = 1-std::exp(-1/(.060*rate));
        reset();
    }
    void reset() noexcept { pending_=hand_=0; motor_=1; touched_=false; }
    void touch(bool held) noexcept {
        touched_=held;
        if (!held) pending_=0; // do not store a backlog of scratch travel
    }
    void move(int ticks, bool coarse) noexcept {
        const double seconds=touched_ ? .004 : .0004;
        pending_=std::clamp(pending_+ticks*seconds*(coarse?8:1),-.15,.15);
    }
    float next() noexcept {
        const double impulse=pending_/.012;
        pending_-=impulse/rate_;
        const double drive=std::clamp(impulse,touched_?-8.:-.35,touched_?8.:.35);
        hand_+=(touched_?handAlpha_:motorAlpha_)*(drive-hand_);
        motor_+=(touched_?brakeAlpha_:motorAlpha_)*((touched_?0.:1.)-motor_);
        return static_cast<float>(motor_+hand_);
    }
private:
    double rate_=48000,handAlpha_=0,brakeAlpha_=0,motorAlpha_=0;
    double pending_=0,hand_=0,motor_=1;
    bool touched_=false;
};

class SampleDecksEngine {
public:
    bool prepare(double rate, uint32_t frames) {
        rate_ = rate; maximum_ = frames;
        if (!engine_.prepare(rate, frames, false, 2, true)) return false;
        try {
            for (auto& deck : audio_) for (auto& channel : deck) channel.assign(frames, 0);
            for (auto& channel : discard_) channel.assign(frames, 0);
            for (auto& roll : rolls_) roll.prepare(rate);
        } catch (...) { return false; }
        for (auto& effect : effects_) if (!effect.prepare(rate, frames)) return false;
        reset(); return true;
    }
    void reset() noexcept {
        engine_.reset(); for (auto& e : effects_) e.reset();
        smoothGain_ = {}; initialized_ = false;
        eq_ = {};
        lastSample_ = {}; heldSample_ = {}; transition_ = {}; sourceFade_ = {};
        for(auto& roll:rolls_)roll.reset();
    }
    void stop(unsigned deck) noexcept { if (deck < 2) {
        sourceFade_[deck]=static_cast<unsigned>(std::ceil(kDeckCueFadeSeconds*rate_));
        heldSample_[deck]=lastSample_[deck];transition_[deck]=static_cast<unsigned>(std::ceil(.003*rate_));
        engine_.resetPad(deck);effects_[deck].reset();eq_[deck]={};
        rolls_[deck].reset();
    } }
    // Relaunch a source gesture without wiping FX tails or rendered history.
    void stopVoices(unsigned deck) noexcept {if(deck<2){
        sourceFade_[deck]=static_cast<unsigned>(std::ceil(kDeckCueFadeSeconds*rate_));
        heldSample_[deck]=lastSample_[deck];transition_[deck]=static_cast<unsigned>(std::ceil(.003*rate_));
        engine_.resetPad(deck);
    }}
    DeckRoll& roll(unsigned deck) noexcept {return rolls_[deck];}
    void seek(unsigned deck, double position) noexcept {
        if(deck>=2)return;
        sourceFade_[deck]=static_cast<unsigned>(std::ceil(kDeckCueFadeSeconds*rate_));
        heldSample_[deck]=lastSample_[deck];transition_[deck]=static_cast<unsigned>(std::ceil(.003*rate_));
        engine_.seekSource(deck,position);
    }
    void setSource(unsigned deck, const SampleAsset* asset, const WavesetMap* map) noexcept {
        if (deck >= 2) return;
        engine_.setPreparedAsset(deck, asset); engine_.setPreparedWavesets(deck, map);
    }
    bool active(unsigned deck) const noexcept { return engine_.slotPlaybackActive(deck); }
    unsigned noteCount(unsigned deck) const noexcept { return engine_.activeNoteCount(deck); }
    unsigned cursorCount(unsigned deck) const noexcept { return engine_.voiceCursorCount(deck); }
    const auto& cursors(unsigned deck) const noexcept { return engine_.voiceCursors(deck); }
    float stackPosition(unsigned deck) const noexcept { return engine_.stackPosition(deck); }
    float stackPhase(unsigned deck, const SampleNeonSettings& s) const noexcept { return engine_.stackPathPhase(deck,s); }
    auto motionVisual(unsigned deck,const SampleNeonSettings& s) const noexcept {return engine_.motionVisual(deck,s);}
    int waveformLayer(unsigned deck,const SampleNeonSettings& s) const noexcept {return engine_.stackWaveformLayer(deck,s);}
    double scanPosition(unsigned deck,const SampleNeonSettings& s) const noexcept {return engine_.motionPosition(deck,s);}
    const float* deckAudio(unsigned deck, unsigned channel) const noexcept { return audio_[deck][channel].data(); }

    void render(const SampleNeonSettings& settings, const SampleNeonEvent* events, std::size_t count,
        const SampleDecksMix& mix, float* const* output, unsigned frames) noexcept {
        if (!output || frames > maximum_) return;
        for (auto& d : audio_) for (auto& c : d) std::fill_n(c.data(), frames, 0.f);
        std::array<float*,32> discard {};
        for (unsigned ch = 0; ch < 32; ++ch) { discard[ch] = discard_[ch].data(); std::fill_n(output[ch],frames,0.f); }
        engine_.render(settings,events,count,discard.data(),32,frames,&receive,this);
        const unsigned width = sampleNeonBusWidth(settings.outputLayout);
        for (unsigned d = 0; d < 2; ++d) {
            std::array<float*,16> channels {};
            for (unsigned ch = 0; ch < width; ++ch) channels[ch] = audio_[d][ch].data();
            // Cue/seek timing stays exact, even for DC-offset or multichannel
            // sources with no shared crossing. Fade the incoming source before
            // EQ/FX; the existing output bridge also eases the outgoing edge.
            for (unsigned n = 0; n < frames && sourceFade_[d]; ++n) {
                const float gain = 1 - sourceFade_[d] / static_cast<float>(std::ceil(kDeckCueFadeSeconds*rate_));
                for (unsigned ch = 0; ch < width; ++ch) audio_[d][ch][n] *= gain;
                --sourceFade_[d];
            }
            mixerEq(d, width, frames, mix);
            const auto& s = settings.slots[d];
            const float amount = std::clamp(s.mangle + engine_.slotPressure(d)*s.pressureDepth,0.f,1.f);
            // A stopped deck cannot leak an old effect tail into a new take.
            effects_[d].process(channels.data(),width,frames,s.character,s.fx,amount,
                s.sourceFormat == SampleNeonSourceFormat::Ambisonic,settings.hostTempoBpm);
            for(unsigned n=0;n<frames;++n){const float held=transition_[d]/static_cast<float>(std::ceil(.003*rate_));
                for(unsigned ch=0;ch<width;++ch){auto& sample=audio_[d][ch][n];sample=sample*(1-held)+heldSample_[d][ch]*held;lastSample_[d][ch]=sample;}
                if(transition_[d])--transition_[d];}
            rolls_[d].process(channels.data(),width,frames);
        }
        const auto gains = deckFadeGains(mix.crossfade,mix.curve);
        const float out = std::pow(10.f, std::clamp(mix.gainDecibels,-60.f,12.f)/20.f);
        const float smoothing = static_cast<float>(1-std::exp(-1/(.003*rate_)));
        if (!initialized_) { for (unsigned d=0;d<2;++d) smoothGain_[d]=gains[d]*mix.levels[d]*out; initialized_=true; }
        for (unsigned n=0;n<frames;++n) {
            for (unsigned d=0;d<2;++d) smoothGain_[d]+=smoothing*(gains[d]*mix.levels[d]*out-smoothGain_[d]);
            for (unsigned ch=0;ch<width;++ch) {
                output[ch][n]=audio_[0][ch][n]*smoothGain_[0]+audio_[1][ch][n]*smoothGain_[1];
                if (mix.stems && width*3<=32) {
                    output[width+ch][n]=audio_[0][ch][n]*mix.levels[0];
                    output[width*2+ch][n]=audio_[1][ch][n]*mix.levels[1];
                }
            }
        }
    }
private:
    struct Eq {
        std::array<float,16> low {}, high {}, filterLow {}, filterHigh {};
        std::array<float,4> gains {{1,1,1,1}};
        float sweep = 0;
    };
    void mixerEq(unsigned d, unsigned channels, unsigned frames, const SampleDecksMix& mix) noexcept {
        // Complementary bands sum exactly to the original at unity. Identical
        // coefficients on every channel preserve an ACN/SN3D field.
        auto& e=eq_[d];
        const float aLow=1-std::exp(float(-6.28318530718*250/rate_));
        const float aHigh=1-std::exp(float(-6.28318530718*2500/rate_));
        const float smooth=1-std::exp(float(-1/(.005*rate_)));
        const std::array<float,4> target {{std::pow(10.f,mix.lowDb[d]/20),std::pow(10.f,mix.midDb[d]/20),
            std::pow(10.f,mix.highDb[d]/20),std::pow(10.f,mix.trimDb[d]/20)}};
        for(unsigned n=0;n<frames;++n) {
            for(unsigned i=0;i<4;++i)e.gains[i]+=smooth*(target[i]-e.gains[i]);
            e.sweep+=smooth*(mix.filter[d]-e.sweep);
            const float distance=std::abs(e.sweep),wet=std::min(1.f,distance*12);
            const float lp=1-std::exp(float(-6.28318530718*std::min(rate_*.45,18000.*std::pow(40./18000.,distance))/rate_));
            const float hp=1-std::exp(float(-6.28318530718*std::min(rate_*.45,20.*std::pow(600.,distance))/rate_));
            for(unsigned ch=0;ch<channels;++ch) {
                const float x=audio_[d][ch][n]*e.gains[3];
                e.low[ch]+=aLow*(x-e.low[ch]);e.high[ch]+=aHigh*(x-e.high[ch]);
                const float high=x-e.high[ch],mid=x-e.low[ch]-high;
                const float eq=e.low[ch]*e.gains[0]+mid*e.gains[1]+high*e.gains[2];
                e.filterLow[ch]+=lp*(eq-e.filterLow[ch]);e.filterHigh[ch]+=hp*(eq-e.filterHigh[ch]);
                const float filtered=e.sweep<0?e.filterLow[ch]:eq-e.filterHigh[ch];
                audio_[d][ch][n]=eq+wet*(filtered-eq);
            }
        }
    }
    static void receive(void* context, unsigned deck, float* const* audio, unsigned channels, uint32_t frames) noexcept {
        if (deck>=2) return;
        auto& self=*static_cast<SampleDecksEngine*>(context);
        for (unsigned ch=0;ch<channels;++ch) std::copy_n(audio[ch],frames,self.audio_[deck][ch].data());
        if (channels==1) std::copy_n(audio[0],frames,self.audio_[deck][1].data());
    }
    SampleNeonPolyEngine engine_;
    std::array<DeckCharacterFx,2> effects_;
    std::array<DeckRoll,2> rolls_;
    std::array<Eq,2> eq_ {};
    std::array<std::array<float,16>,2> lastSample_ {},heldSample_ {};
    std::array<unsigned,2> transition_ {},sourceFade_ {};
    std::array<std::array<std::vector<float>,16>,2> audio_;
    std::array<std::vector<float>,32> discard_;
    std::array<float,2> smoothGain_ {};
    double rate_=48000; unsigned maximum_=0; bool initialized_=false;
};

} // namespace s3g::sample
