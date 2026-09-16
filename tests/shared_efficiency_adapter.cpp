#include "shared_efficiency_api.h"
#if defined(S3G_DSP_EFFICIENCY_REFERENCE)
#define s3g s3g_efficiency_reference
#define EFFICIENCY_FACTORY efficiencyReference
#else
#define EFFICIENCY_FACTORY efficiencyCandidate
#endif
#include "s3g_ambi_water_presets.h"
#include "s3g_ambi_wind_presets.h"
#include "s3g_ambi_insect_presets.h"
#include "s3g_ambi_pyrosphere_presets.h"
#include "s3g_acapella_resonator_bank.h"
#include "s3g_spectral_mesh.h"
#include <stdexcept>
#include <type_traits>

namespace {
template<class Engine, auto Preset, unsigned Count>
class EnvironmentProbe final : public EfficiencyProbe {
    Engine engine;
public:
    void prepare(double rate, unsigned) override { engine.prepare(rate); }
    void reset() override { engine.reset(); }
    unsigned presets() const override { return Count; }
    unsigned channels() const override { return 64; }
    void configure(unsigned preset, unsigned variant, unsigned voices, unsigned order) override {
        auto p = Preset(preset);
        p.voices = voices; p.order = order;
        if (variant) {
            const float x = float(variant % 17) / 16.f;
            p.centerAzimuthDeg = x * 360.f - 180.f;
            p.centerElevationDeg = x * 150.f - 75.f;
            p.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(variant % 4);
            p.fieldListenAmount = .8f;
            p.outputGainDb = -12.f + x * 6.f;
            if constexpr (std::is_same_v<Engine, s3g::AmbiPyrosphereEncoder>) {
                p.body=x; p.air=1.f-x; p.sweep=x*.7f; p.shrill=x*.8f;
                p.q=1.f-x; p.particles=x; p.pressure=x; p.material=x;
            }
        }
        engine.setParams(p);
        engine.beginTransition();
    }
    void process(const float* const*, float* const* output, unsigned frames) override {
        engine.process(output, 64, frames);
    }
    std::vector<double> inspect() const override {
        std::vector<double> result;
        for (unsigned v=0; v<64; ++v) {
            const auto point=engine.voicePoint(v);
            result.insert(result.end(), {engine.voiceEnergy(v), point.azimuthDeg,
                point.elevationDeg, point.distance});
        }
        return result;
    }
};

class FormantProbe final : public EfficiencyProbe {
    s3g::AcapellaResonatorBank engine;
public:
    void prepare(double rate, unsigned) override { engine.prepare(rate); }
    void reset() override { engine.reset(); }
    unsigned presets() const override { return 4; }
    unsigned channels() const override { return 2; }
    void configure(unsigned preset, unsigned variant, unsigned, unsigned) override {
        s3g::AcapellaResonatorParams p;
        p.bandLayout = (preset & 1) ? s3g::AcapellaResonatorBandLayout::Wide16
                                   : s3g::AcapellaResonatorBandLayout::Speech22;
        p.analysisSlope = (preset & 2) ? s3g::AcapellaResonatorAnalysisSlope::EightPole
                                      : s3g::AcapellaResonatorAnalysisSlope::FourPole;
        p.definition = .9f; p.consonantColor = .7f; p.consonantSpeed = .35f;
        p.analysisWidth = .2f + float(variant % 7) * .1f;
        p.carrierNoise = .3f;
        engine.setParams(p);
        s3g::AcapellaResonatorGesture g;
        g.active = true; g.phoneme = s3g::AcapellaPhoneme::S;
        g.frequencyHz = 110.f + float(variant % 9) * 20.f;
        g.voiceInstance = 1;
        engine.setGesture(g);
    }
    void process(const float* const* input, float* const* output, unsigned frames) override {
        for (unsigned f=0; f<frames; ++f) {
            const auto value=engine.processFrameStereo(input[0][f], input[1][f]);
            output[0][f]=value.left; output[1][f]=value.right;
        }
    }
    std::vector<double> inspect() const override {
        const auto value=engine.meterSnapshot();
        std::vector<double> result {double(value.activeBands)};
        for (auto x : value.analysis) result.push_back(x);
        for (auto x : value.synthesis) result.push_back(x);
        return result;
    }
};

class SpectralProbe final : public EfficiencyProbe {
    s3g::SpectralMeshProcessor engine;
    unsigned width;
public:
    explicit SpectralProbe(unsigned channels) : width(channels) {}
    void prepare(double rate, unsigned fftSize) override {
        if (!engine.prepare(rate, width, fftSize, 4, 512)) throw std::runtime_error("FFT prepare failed");
    }
    void reset() override { engine.reset(); }
    unsigned presets() const override { return 3; }
    unsigned channels() const override { return width; }
    void configure(unsigned preset, unsigned variant, unsigned, unsigned) override {
        for (unsigned ch=0; ch<width; ++ch) {
            s3g::SpectralSprayParams p;
            p.loFreq = 30.f + float((variant + ch) % 5) * 170.f;
            p.hiFreq = 4500.f + float((variant + ch) % 7) * 1300.f;
            p.mix = .8f;
            engine.setLaneParams(ch, p);
        }
        s3g::TopologyState topology;
        topology.amount = preset == 0 ? 0. : .6;
        engine.setTopologyState(topology);
        engine.setPropagation(preset == 2 ? .7f : 1.f, .2f, .1f);
        if (variant == 13) engine.requestCapture();
    }
    void process(const float* const* input, float* const* output, unsigned frames) override {
        engine.process(input, width, output, width, frames);
    }
    std::vector<double> inspect() const override { return {}; }
};
}

std::unique_ptr<EfficiencyProbe> EFFICIENCY_FACTORY(EfficiencyEngine kind) {
    switch(kind) {
    case EfficiencyEngine::Water:
        return std::make_unique<EnvironmentProbe<s3g::AmbiWaterEncoder,
            s3g::ambiWaterFactoryPreset, s3g::kAmbiWaterFactoryPresetCount>>();
    case EfficiencyEngine::Wind:
        return std::make_unique<EnvironmentProbe<s3g::AmbiWindEncoder,
            s3g::ambiWindFactoryPreset, s3g::kAmbiWindFactoryPresetCount>>();
    case EfficiencyEngine::Insect:
        return std::make_unique<EnvironmentProbe<s3g::AmbiInsectEncoder,
            s3g::ambiInsectFactoryPreset, s3g::kAmbiInsectFactoryPresetCount>>();
    case EfficiencyEngine::Pyrosphere:
        return std::make_unique<EnvironmentProbe<s3g::AmbiPyrosphereEncoder,
            s3g::ambiPyrosphereFactoryPreset, s3g::kAmbiPyrosphereFactoryPresetCount>>();
    case EfficiencyEngine::Formant: return std::make_unique<FormantProbe>();
    case EfficiencyEngine::Spectral8: return std::make_unique<SpectralProbe>(8);
    case EfficiencyEngine::Spectral24: return std::make_unique<SpectralProbe>(24);
    }
    throw std::runtime_error("unknown engine");
}
