#pragma once
#include <cstdint>
#include <memory>
#include <vector>

enum class EfficiencyEngine { Water, Wind, Insect, Pyrosphere, Formant, Spectral8, Spectral24 };

// Separate translation units/namespaces prevent inline reference and optimized
// implementations from being merged by the linker. Nothing here is plugin ABI.
struct EfficiencyProbe {
    virtual ~EfficiencyProbe() = default;
    virtual void prepare(double rate, unsigned fftSize) = 0;
    virtual void reset() = 0;
    virtual unsigned presets() const = 0;
    virtual unsigned channels() const = 0;
    virtual void configure(unsigned preset, unsigned variant, unsigned voices, unsigned order) = 0;
    virtual void process(const float* const* input, float* const* output, unsigned frames) = 0;
    virtual std::vector<double> inspect() const = 0;
};
std::unique_ptr<EfficiencyProbe> efficiencyReference(EfficiencyEngine);
std::unique_ptr<EfficiencyProbe> efficiencyCandidate(EfficiencyEngine);
