#include "s3g_ambi_neural_ecology.h"
#include "s3g_ambi_neural_field_lattice.h"
#include "s3g_ambi_neural_ecology_presets.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr uint32_t kFrames = 256u;
using Buffer = std::array<std::array<float, kFrames>, s3g::kAmbiNeuralEcologyMaxChannels>;

float processBlock(s3g::AmbiNeuralEcology& engine, Buffer& buffer, uint32_t channels = 64u)
{
    std::array<float*, s3g::kAmbiNeuralEcologyMaxChannels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) outputs[channel] = buffer[channel].data();
    engine.process(outputs.data(), channels, kFrames);
    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        for (float value : buffer[channel]) {
            if (!std::isfinite(value)) return 1000.0f;
            peak = std::max(peak, std::fabs(value));
        }
    }
    return peak;
}

bool testPresets()
{
    Buffer buffer {};
    for (uint32_t preset = 0u; preset < s3g::kAmbiNeuralEcologyFactoryPresetCount; ++preset) {
        s3g::AmbiNeuralEcology engine;
        engine.prepare(48000.0);
        engine.setParams(s3g::ambiNeuralEcologyFactoryPreset(preset));
        engine.reset();
        float peak = 0.0f;
        for (uint32_t block = 0u; block < 160u; ++block) peak = std::max(peak, processBlock(engine, buffer));
        if (!(peak > 1.0e-7f) || peak > 8.0f) {
            std::cerr << "Neural Ecology preset " << preset << " produced invalid peak " << peak << "\n";
            return false;
        }
    }
    return true;
}

bool testRandomizedAudibility()
{
    constexpr uint32_t kSeedsPerNodeSet = 8u;
    constexpr uint32_t kWarmupBlocks = 32u;
    constexpr uint32_t kRenderBlocks = 192u;
    constexpr double kMinimumRms = 1.0e-3;
    constexpr float kMinimumPeak = 3.0e-3f;

    Buffer buffer {};
    double quietestRms = 1.0;
    float quietestPeak = 1.0f;
    for (uint32_t set = 0u; set < 5u; ++set) {
        const auto nodeSet = static_cast<s3g::AmbiNeuralNodeSet>(set);
        for (uint32_t trial = 0u; trial < kSeedsPerNodeSet; ++trial) {
            auto current = s3g::ambiNeuralEcologyFactoryPreset(0u);
            current.order = 3u;
            current.nodeSet = nodeSet;
            current.pickupSet = s3g::AmbiNeuralPickupSet::Cube8;
            current.scoreMode = s3g::AmbiNeuralScoreMode::Field;
            current.scorePlanes = s3g::AmbiNeuralScorePlanes::Eight;
            current.scoreAmount = 0.72f;
            current.scoreDwellSeconds = 11.0f;
            current.scoreTransitionSeconds = 2.5f;
            current.outputGainDb = -18.0f;
            const uint32_t initialSeed = 0x41554430u + set * 0x100u + trial;
            uint32_t randomSeed = initialSeed;
            const auto params = s3g::randomizeAmbiNeuralEcologyParams(
                current, randomSeed, false, false);
            const auto lattice = s3g::randomAmbiNeuralLattice(
                randomSeed, s3g::ambiNeuralScorePlaneCount(params.scorePlanes));
            const auto renderParams = s3g::applyAmbiNeuralLatticeCell(params,
                lattice.cells[lattice.currentCell], params.scoreAmount);

            if (params.order != current.order || params.nodeSet != current.nodeSet
                || params.pickupSet != current.pickupSet
                || params.scoreMode != current.scoreMode
                || params.scorePlanes != current.scorePlanes
                || params.scoreAmount != current.scoreAmount
                || params.scoreDwellSeconds != current.scoreDwellSeconds
                || params.scoreTransitionSeconds != current.scoreTransitionSeconds
                || params.outputGainDb != current.outputGainDb) {
                std::cerr << "Neural Ecology randomization changed a performer-owned structural control\n";
                return false;
            }
            if (lattice.planeCount != 8u
                || lattice.currentCell >= 8u * s3g::kAmbiNeuralLatticeCellsPerPlane) {
                std::cerr << "Neural Ecology randomization did not preserve the eight-plane score\n";
                return false;
            }
            if (params.nodeSet == s3g::AmbiNeuralNodeSet::Field64
                && (params.registerSemitones < 12.0f || params.registerSemitones > 30.0f)) {
                std::cerr << "Neural Ecology randomized Field 64 outside its audible register: "
                          << params.registerSemitones << "\n";
                return false;
            }
            if (randomSeed == initialSeed) {
                std::cerr << "Neural Ecology randomization did not advance its seed\n";
                return false;
            }

            s3g::AmbiNeuralEcology engine;
            engine.prepare(48000.0);
            engine.setParams(renderParams);
            engine.reset();

            double sumSquares = 0.0;
            uint64_t sampleCount = 0u;
            float peak = 0.0f;
            for (uint32_t block = 0u; block < kRenderBlocks; ++block) {
                const float blockPeak = processBlock(engine, buffer, 16u);
                if (blockPeak >= 1000.0f) {
                    std::cerr << "Neural Ecology randomized render produced non-finite audio\n";
                    return false;
                }
                if (block < kWarmupBlocks) continue;
                peak = std::max(peak, blockPeak);
                for (uint32_t channel = 0u; channel < 16u; ++channel) {
                    for (float value : buffer[channel]) {
                        sumSquares += static_cast<double>(value) * value;
                        ++sampleCount;
                    }
                }
            }
            const double rms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
            quietestRms = std::min(quietestRms, rms);
            quietestPeak = std::min(quietestPeak, peak);
            if (!(rms >= kMinimumRms) || !(peak >= kMinimumPeak) || peak > 8.0f) {
                std::cerr << "Neural Ecology randomized node set " << set << ", trial " << trial
                          << " failed audibility: rms " << rms << ", peak " << peak
                          << ", register " << params.registerSemitones
                          << ", lattice cell " << lattice.currentCell << "\n";
                return false;
            }
        }
    }
    std::cout << "Random ecology floor: rms " << quietestRms
              << ", peak " << quietestPeak << "\n";
    return true;
}

bool testNodeSetsAndRouting()
{
    static constexpr std::array<uint32_t, 5> expected {{ 4u, 8u, 16u, 32u, 64u }};
    Buffer buffer {};
    s3g::AmbiNeuralEcology engine;
    engine.prepare(48000.0);
    auto params = s3g::ambiNeuralEcologyFactoryPreset(0u);
    params.order = 2u;
    for (uint32_t set = 0u; set < expected.size(); ++set) {
        params.nodeSet = static_cast<s3g::AmbiNeuralNodeSet>(set);
        engine.setParams(params);
        if (engine.activeNodeCount() != expected[set]) {
            std::cerr << "Neural Ecology node set " << set << " reports " << engine.activeNodeCount()
                      << " nodes instead of " << expected[set] << "\n";
            return false;
        }
        for (uint32_t block = 0u; block < 64u; ++block) processBlock(engine, buffer);
        for (uint32_t channel = 9u; channel < 64u; ++channel) {
            for (float value : buffer[channel]) {
                if (value != 0.0f) {
                    std::cerr << "Neural Ecology failed to clear channel " << channel << " above 2OA\n";
                    return false;
                }
            }
        }
    }
    return true;
}

bool testFieldListening()
{
    Buffer returnBuffer {};
    Buffer dryBuffer {};
    auto params = s3g::ambiNeuralEcologyFactoryPreset(3u);
    params.order = 3u;
    params.brownian = 0.0f;
    params.drift = 0.0f;
    params.plasticity = 0.0f;
    params.fieldReturn = 0.82f;
    params.propagationMs = 18.0f;

    s3g::AmbiNeuralEcology returned;
    s3g::AmbiNeuralEcology dry;
    returned.prepare(48000.0);
    dry.prepare(48000.0);
    returned.setParams(params);
    auto dryParams = params;
    dryParams.fieldReturn = 0.0f;
    dry.setParams(dryParams);
    returned.reset();
    dry.reset();

    double difference = 0.0;
    float pickupPeak = 0.0f;
    for (uint32_t block = 0u; block < 320u; ++block) {
        processBlock(returned, returnBuffer, 16u);
        processBlock(dry, dryBuffer, 16u);
        for (uint32_t channel = 0u; channel < 16u; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                difference += std::fabs(returnBuffer[channel][frame] - dryBuffer[channel][frame]);
            }
        }
        for (uint32_t pickup = 0u; pickup < 4u; ++pickup) {
            pickupPeak = std::max(pickupPeak, std::fabs(returned.pickupValue(pickup)));
        }
    }
    if (!(pickupPeak > 1.0e-7f)) {
        std::cerr << "Neural Ecology virtual pickups did not hear the HOA field\n";
        return false;
    }
    if (!(difference > 0.01)) {
        std::cerr << "Neural Ecology field return had no material effect: " << difference << "\n";
        return false;
    }
    return true;
}

bool testAdaptiveListening()
{
    Buffer localBuffer {};
    Buffer crossBuffer {};
    auto params = s3g::ambiNeuralEcologyFactoryPreset(3u);
    params.order = 1u;
    params.brownian = 0.0f;
    params.drift = 0.0f;
    params.plasticity = 0.0f;
    params.auditoryPlasticity = 0.0f;
    params.adaptation = 0.0f;
    params.fieldReturn = 0.82f;
    params.propagationMs = 9.0f;
    params.listeningMode = s3g::AmbiNeuralListeningMode::Local;

    s3g::AmbiNeuralEcology local;
    s3g::AmbiNeuralEcology cross;
    local.prepare(48000.0);
    cross.prepare(48000.0);
    local.setParams(params);
    auto crossParams = params;
    crossParams.listeningMode = s3g::AmbiNeuralListeningMode::Cross;
    cross.setParams(crossParams);
    local.reset();
    cross.reset();
    if (std::fabs(local.auditoryWeight(0u, 0u) - 1.0f) > 1.0e-7f
        || std::fabs(local.auditoryWeight(0u, 1u)) > 1.0e-7f
        || !(cross.auditoryWeight(0u, 1u) > cross.auditoryWeight(0u, 0u))) {
        std::cerr << "Neural Ecology listening topologies did not establish the expected matrix\n";
        return false;
    }

    double topologyDifference = 0.0;
    for (uint32_t block = 0u; block < 180u; ++block) {
        processBlock(local, localBuffer, 4u);
        processBlock(cross, crossBuffer, 4u);
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                topologyDifference += std::fabs(localBuffer[channel][frame] - crossBuffer[channel][frame]);
            }
        }
    }
    if (!(topologyDifference > 0.01)) {
        std::cerr << "Neural Ecology auditory topology did not affect the sound: "
                  << topologyDifference << "\n";
        return false;
    }

    s3g::AmbiNeuralEcology evolving;
    auto evolvingParams = s3g::ambiNeuralEcologyFactoryPreset(2u);
    evolvingParams.order = 1u;
    evolvingParams.nodeSet = s3g::AmbiNeuralNodeSet::Cell16;
    evolvingParams.listeningMode = s3g::AmbiNeuralListeningMode::Diffuse;
    evolvingParams.plasticityMode = s3g::AmbiNeuralPlasticityMode::Reinforce;
    evolvingParams.plasticity = 0.0f;
    evolvingParams.auditoryPlasticity = 1.0f;
    evolvingParams.metabolism = 0.92f;
    evolvingParams.adaptation = 1.0f;
    evolvingParams.fieldReturn = 0.72f;
    evolving.prepare(48000.0);
    evolving.setParams(evolvingParams);
    evolving.reset();
    const auto initialGenome = evolving.genomeValues();
    for (uint32_t block = 0u; block < 240u; ++block) processBlock(evolving, localBuffer, 4u);
    const auto evolvedGenome = evolving.genomeValues();
    double auditoryChange = 0.0;
    for (uint32_t index = 80u; index < 112u; ++index) {
        auditoryChange += std::fabs(evolvedGenome[index] - initialGenome[index]);
    }
    float homeostaticChange = 0.0f;
    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
        homeostaticChange = std::max(homeostaticChange, std::fabs(evolving.homeostaticBias(lobe)));
    }
    if (!(auditoryChange > 1.0e-6) || !(homeostaticChange > 1.0e-4f)) {
        std::cerr << "Neural Ecology listening failed to learn or regulate: matrix "
                  << auditoryChange << ", homeostasis " << homeostaticChange << "\n";
        return false;
    }

    evolvingParams.freeze = 1u;
    evolving.setParams(evolvingParams);
    const auto frozenGenome = evolving.genomeValues();
    for (uint32_t block = 0u; block < 60u; ++block) processBlock(evolving, localBuffer, 4u);
    const auto afterFreeze = evolving.genomeValues();
    for (uint32_t index = 0u; index < frozenGenome.size(); ++index) {
        if (frozenGenome[index] != afterFreeze[index]) {
            std::cerr << "Neural Ecology freeze did not suspend genome evolution\n";
            return false;
        }
    }

    evolving.reset();
    evolving.restoreGenome(evolvedGenome);
    const auto restoredGenome = evolving.genomeValues();
    for (uint32_t index = 0u; index < evolvedGenome.size(); ++index) {
        if (std::fabs(evolvedGenome[index] - restoredGenome[index]) > 1.0e-7f) {
            std::cerr << "Neural Ecology genome did not restore exactly\n";
            return false;
        }
    }
    return true;
}

bool testDeterminismAndMutation()
{
    Buffer aBuffer {};
    Buffer bBuffer {};
    s3g::AmbiNeuralEcology a;
    s3g::AmbiNeuralEcology b;
    auto params = s3g::ambiNeuralEcologyFactoryPreset(4u);
    params.order = 1u;
    params.plasticity = 0.0f;
    a.prepare(48000.0);
    b.prepare(48000.0);
    a.setParams(params);
    b.setParams(params);
    a.reset();
    b.reset();
    for (uint32_t block = 0u; block < 80u; ++block) {
        processBlock(a, aBuffer, 4u);
        processBlock(b, bBuffer, 4u);
        for (uint32_t channel = 0u; channel < 4u; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                if (std::fabs(aBuffer[channel][frame] - bBuffer[channel][frame]) > 1.0e-7f) {
                    std::cerr << "Neural Ecology lost seeded determinism\n";
                    return false;
                }
            }
        }
    }

    ++params.mutate;
    a.setParams(params);
    double difference = 0.0;
    for (uint32_t block = 0u; block < 80u; ++block) {
        processBlock(a, aBuffer, 4u);
        processBlock(b, bBuffer, 4u);
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            difference += std::fabs(aBuffer[0][frame] - bBuffer[0][frame]);
        }
    }
    if (!(difference > 0.001)) {
        std::cerr << "Neural Ecology mutation did not alter the organism\n";
        return false;
    }
    return true;
}

bool testPickupSetsAndGenomes()
{
    Buffer buffer {};
    auto params = s3g::ambiNeuralEcologyFactoryPreset(4u);
    params.order = 3u;
    params.brownian = 0.0f;
    params.drift = 0.0f;
    params.plasticity = 0.0f;
    params.auditoryPlasticity = 0.0f;
    params.adaptation = 0.0f;
    params.pickupSet = s3g::AmbiNeuralPickupSet::Tetra4;

    s3g::AmbiNeuralEcology engine;
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    if (engine.activePickupCount() != 4u) {
        std::cerr << "Neural Ecology tetrahedral aperture did not report four pickups\n";
        return false;
    }
    params.pickupSet = s3g::AmbiNeuralPickupSet::Cube8;
    engine.setParams(params);
    if (engine.activePickupCount() != 8u) {
        std::cerr << "Neural Ecology cube aperture did not report eight pickups\n";
        return false;
    }
    float upperPickupPeak = 0.0f;
    for (uint32_t block = 0u; block < 180u; ++block) {
        processBlock(engine, buffer, 16u);
        for (uint32_t pickup = 4u; pickup < 8u; ++pickup) {
            upperPickupPeak = std::max(upperPickupPeak, std::fabs(engine.pickupValue(pickup)));
        }
    }
    if (!(upperPickupPeak > 1.0e-7f)) {
        std::cerr << "Neural Ecology cube-corner pickups did not hear the HOA field\n";
        return false;
    }

    params.pickupAdapt = 1.0f;
    params.pickupAnchor = 0.0f;
    params.fieldReturn = 0.86f;
    params.auditoryPlasticity = 0.62f;
    engine.setParams(params);
    engine.reset();
    for (uint32_t block = 0u; block < 320u; ++block) processBlock(engine, buffer, 16u);
    const auto adaptedGenome = engine.genomeValues();
    double steeringAmount = 0.0;
    for (uint32_t index = 117u; index < adaptedGenome.size(); ++index) {
        steeringAmount += std::fabs(adaptedGenome[index]);
    }
    float closestPair = -1.0f;
    for (uint32_t first = 0u; first < 8u; ++first) {
        const auto aPoint = engine.pickupDirection(first);
        const auto aDirection = s3g::directionFromAed(aPoint.azimuthDeg, aPoint.elevationDeg);
        for (uint32_t second = first + 1u; second < 8u; ++second) {
            const auto bPoint = engine.pickupDirection(second);
            const auto bDirection = s3g::directionFromAed(bPoint.azimuthDeg, bPoint.elevationDeg);
            closestPair = std::max(closestPair,
                aDirection.x * bDirection.x + aDirection.y * bDirection.y + aDirection.z * bDirection.z);
        }
    }
    if (!(steeringAmount > 0.02) || !(closestPair < 0.95f)) {
        std::cerr << "Neural Ecology adaptive auditory body did not learn safely: steering "
                  << steeringAmount << ", closest dot " << closestPair << "\n";
        return false;
    }
    params.pickupAdapt = 0.0f;
    params.pickupAnchor = 1.0f;
    engine.setParams(params);
    for (uint32_t block = 0u; block < 320u; ++block) processBlock(engine, buffer, 16u);
    const auto anchoredGenome = engine.genomeValues();
    double anchoredAmount = 0.0;
    for (uint32_t index = 117u; index < anchoredGenome.size(); ++index) {
        anchoredAmount += std::fabs(anchoredGenome[index]);
    }
    if (!(anchoredAmount < steeringAmount * 0.72)) {
        std::cerr << "Neural Ecology pickup anchor did not return learned ears: "
                  << steeringAmount << " -> " << anchoredAmount << "\n";
        return false;
    }

    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA {};
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB {};
    genomeA[0u] = -0.30f;
    genomeB[0u] = 0.30f;
    genomeA[80u] = -0.50f;
    genomeB[80u] = 0.50f;
    genomeA[112u] = -0.20f;
    genomeB[112u] = 0.20f;
    genomeA[116u] = 0.95f;
    genomeB[116u] = 0.05f;
    genomeA[117u] = -0.60f;
    genomeB[117u] = 0.60f;
    engine.setGenomeSlot(0u, genomeA);
    engine.setGenomeSlot(1u, genomeB);
    params.genomeMorph = 0.5f;
    engine.setParams(params);
    const auto midpoint = engine.morphedGenome();
    if (std::fabs(midpoint[0u]) > 1.0e-7f || std::fabs(midpoint[80u]) > 1.0e-7f
        || std::fabs(midpoint[112u]) > 1.0e-7f || std::fabs(midpoint[117u]) > 1.0e-7f
        || std::min(midpoint[116u], 1.0f - midpoint[116u]) > 1.0e-6f) {
        std::cerr << "Neural Ecology A/B morph did not interpolate genome values or wrapped phase\n";
        return false;
    }

    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> target {};
    target[0u] = 0.32f;
    target[80u] = 0.48f;
    target[112u] = 0.18f;
    target[117u] = 0.72f;
    engine.restoreGenome({});
    engine.setGenomeSlot(0u, target);
    engine.setGenomeSlot(1u, {}, false);
    params.genomeMorph = 0.0f;
    params.heredity = 1.0f;
    engine.setParams(params);
    for (uint32_t block = 0u; block < 160u; ++block) processBlock(engine, buffer, 16u);
    const auto inherited = engine.genomeValues();
    if (!(inherited[0u] > 0.08f) || !(inherited[80u] > 0.12f)
        || !(inherited[112u] > 0.04f) || !(inherited[117u] > 0.18f)) {
        std::cerr << "Neural Ecology heredity did not attract the living genome toward its parent\n";
        return false;
    }

    s3g::AmbiNeuralEcology zeroDepth;
    s3g::AmbiNeuralEcology fullDepth;
    zeroDepth.prepare(48000.0);
    fullDepth.prepare(48000.0);
    auto mutationParams = s3g::ambiNeuralEcologyFactoryPreset(0u);
    mutationParams.plasticity = 0.0f;
    mutationParams.auditoryPlasticity = 0.0f;
    mutationParams.adaptation = 0.0f;
    zeroDepth.setParams(mutationParams);
    fullDepth.setParams(mutationParams);
    zeroDepth.reset();
    fullDepth.reset();
    mutationParams.mutationDepth = 0.0f;
    ++mutationParams.mutate;
    zeroDepth.setParams(mutationParams);
    mutationParams.mutationDepth = 1.0f;
    fullDepth.setParams(mutationParams);
    const auto zeroGenome = zeroDepth.genomeValues();
    const auto fullGenome = fullDepth.genomeValues();
    double fullChange = 0.0;
    double geometryChange = 0.0;
    for (uint32_t index = 0u; index < 112u; ++index) {
        if (zeroGenome[index] != 0.0f) {
            std::cerr << "Neural Ecology zero-depth mutation changed the genome\n";
            return false;
        }
        fullChange += std::fabs(fullGenome[index]);
    }
    for (uint32_t index = 117u; index < fullGenome.size(); ++index) {
        geometryChange += std::fabs(fullGenome[index]);
    }
    if (!(fullChange > 0.1) || !(geometryChange > 0.1)) {
        std::cerr << "Neural Ecology full-depth mutation did not change the living genome\n";
        return false;
    }
    return true;
}

bool testSeventhOrderEnergy()
{
    Buffer buffer {};
    s3g::AmbiNeuralEcology engine;
    auto params = s3g::ambiNeuralEcologyFactoryPreset(4u);
    params.order = 7u;
    engine.prepare(48000.0);
    engine.setParams(params);
    engine.reset();
    double highestOrderEnergy = 0.0;
    for (uint32_t block = 0u; block < 160u; ++block) {
        processBlock(engine, buffer);
        for (uint32_t channel = 49u; channel < 64u; ++channel) {
            for (float value : buffer[channel]) highestOrderEnergy += static_cast<double>(value) * value;
        }
    }
    if (!(highestOrderEnergy > 1.0e-10)) {
        std::cerr << "Neural Ecology produced no seventh-order energy\n";
        return false;
    }
    return true;
}

bool testFieldLattice()
{
    auto storage = s3g::defaultAmbiNeuralLattice();
    double variation = 0.0;
    for (uint32_t cell = 1u; cell < storage.cells.size(); ++cell) {
        for (uint32_t macro = 0u; macro < s3g::kAmbiNeuralLatticeMacros; ++macro) {
            variation += std::fabs(storage.cells[cell].macro[macro]
                - storage.cells[0u].macro[macro]);
        }
    }
    if (!(variation > 2.0)) {
        std::cerr << "Neural Ecology default Field Lattice has no useful variation\n";
        return false;
    }

    uint32_t randomSeedA = 0x4c415454u;
    uint32_t randomSeedB = randomSeedA;
    const auto randomA = s3g::randomAmbiNeuralLattice(randomSeedA, 8u);
    const auto randomB = s3g::randomAmbiNeuralLattice(randomSeedB, 8u);
    double randomVariation = 0.0;
    for (uint32_t cell = 0u; cell < randomA.cells.size(); ++cell) {
        for (uint32_t macro = 0u; macro < s3g::kAmbiNeuralLatticeMacros; ++macro) {
            if (randomA.cells[cell].macro[macro] != randomB.cells[cell].macro[macro]) {
                std::cerr << "Neural Ecology randomized Field Lattice is not deterministic\n";
                return false;
            }
            if (cell > 0u) {
                randomVariation += std::fabs(randomA.cells[cell].macro[macro]
                    - randomA.cells[0u].macro[macro]);
            }
        }
        for (uint32_t direction = 0u;
            direction < s3g::kAmbiNeuralLatticeDirections; ++direction) {
            const uint32_t edge = cell * s3g::kAmbiNeuralLatticeDirections + direction;
            if (randomA.edges[edge] != randomB.edges[edge]) {
                std::cerr << "Neural Ecology randomized lattice folds are not deterministic\n";
                return false;
            }
        }
    }
    if (!(randomVariation > 2.0) || randomSeedA == 0x4c415454u
        || randomSeedA != randomSeedB || randomA.trailCount != 1u
        || randomA.trail[0u] != randomA.currentCell
        || randomA.selectedCell != randomA.currentCell) {
        std::cerr << "Neural Ecology randomized Field Lattice has invalid landscape or reset state\n";
        return false;
    }

    constexpr uint32_t kEightPlaneCells =
        8u * s3g::kAmbiNeuralLatticeCellsPerPlane;
    std::array<bool, kEightPlaneCells> visited {};
    std::array<uint32_t, kEightPlaneCells> queue {};
    uint32_t read = 0u;
    uint32_t write = 1u;
    queue[0] = randomA.currentCell;
    visited[randomA.currentCell] = true;
    while (read < write) {
        const uint32_t cell = queue[read++];
        for (uint32_t direction = 0u;
            direction < s3g::kAmbiNeuralLatticeDirections; ++direction) {
            const uint32_t target =
                randomA.edges[cell * s3g::kAmbiNeuralLatticeDirections + direction];
            if (target >= kEightPlaneCells || visited[target]) continue;
            visited[target] = true;
            queue[write++] = target;
        }
    }
    if (write != kEightPlaneCells) {
        std::cerr << "Neural Ecology randomized eight-plane lattice has unreachable cells\n";
        return false;
    }

    uint32_t foldSource = 0u;
    uint32_t foldDirection = 0u;
    uint32_t foldTarget = 0u;
    bool foundFold = false;
    for (uint32_t cell = 0u; cell < kEightPlaneCells && !foundFold; ++cell) {
        for (uint32_t direction = 0u;
            direction < s3g::kAmbiNeuralLatticeDirections; ++direction) {
            const uint32_t target =
                randomA.edges[cell * s3g::kAmbiNeuralLatticeDirections + direction];
            if (target / s3g::kAmbiNeuralLatticeCellsPerPlane
                != cell / s3g::kAmbiNeuralLatticeCellsPerPlane) {
                foldSource = cell;
                foldDirection = direction;
                foldTarget = target;
                foundFold = true;
                break;
            }
        }
    }
    if (!foundFold) {
        std::cerr << "Neural Ecology eight-plane lattice has no directional folds\n";
        return false;
    }
    auto foldStorage = randomA;
    foldStorage.currentCell = foldSource;
    foldStorage.selectedCell = foldSource;
    foldStorage.trail.fill(foldSource);
    foldStorage.trailCount = 1u;
    s3g::AmbiNeuralFieldLattice foldLattice;
    foldLattice.setStorage(foldStorage);
    foldLattice.requestDirection(
        foldDirection, 1.0f, s3g::AmbiNeuralScoreMode::Midi, 0.10f);
    std::array<float, s3g::kAmbiNeuralEcologyPickups> foldSilence {};
    foldLattice.advance(
        0.20f, foldSilence, 8u, s3g::AmbiNeuralScoreMode::Midi, 1.0f, 0.10f);
    if (foldLattice.currentCell() != foldTarget) {
        std::cerr << "Neural Ecology directional fold did not traverse between planes\n";
        return false;
    }

    s3g::AmbiNeuralFieldLattice lattice;
    lattice.setStorage(storage);
    lattice.requestDirection(2u, 1.0f, s3g::AmbiNeuralScoreMode::Midi, 0.10f);
    std::array<float, s3g::kAmbiNeuralEcologyPickups> silence {};
    lattice.advance(0.20f, silence, 4u, s3g::AmbiNeuralScoreMode::Midi, 1.0f, 0.10f);
    if (lattice.currentCell() != 6u || lattice.storage().trailCount < 2u) {
        std::cerr << "Neural Ecology MIDI lattice direction did not traverse east\n";
        return false;
    }

    lattice.setStorage(storage);
    lattice.requestDirection(2u, 1.0f, s3g::AmbiNeuralScoreMode::Midi, 1.0f);
    lattice.advance(0.10f, silence, 4u, s3g::AmbiNeuralScoreMode::Midi, 1.0f, 1.0f);
    if (!lattice.transitioning()) {
        std::cerr << "Neural Ecology lattice did not begin a stoppable transition\n";
        return false;
    }
    lattice.stop();
    if (lattice.transitioning() || lattice.currentCell() != storage.currentCell
        || lattice.targetCell() != storage.currentCell
        || lattice.transitionProgress() != 1.0f) {
        std::cerr << "Neural Ecology score STOP did not halt at the active cell\n";
        return false;
    }
    const auto stoppedClimate = lattice.climate().macro;
    lattice.requestCell(15u, 0.5f, 1.0f);
    if (!lattice.transitioning() || lattice.currentCell() != storage.currentCell
        || lattice.targetCell() != 15u) {
        std::cerr << "Neural Ecology stopped cell recall did not begin a smooth transition\n";
        return false;
    }
    lattice.advanceTransition(0.20f);
    if (!lattice.transitioning() || lattice.currentCell() != storage.currentCell
        || lattice.climate().macro == stoppedClimate
        || lattice.climate().macro == storage.cells[15u].macro) {
        std::cerr << "Neural Ecology stopped cell recall teleported instead of interpolating\n";
        return false;
    }
    lattice.advanceTransition(1.0f);
    if (lattice.currentCell() != 15u || lattice.targetCell() != 15u
        || lattice.transitioning()
        || lattice.climate().macro != storage.cells[15u].macro) {
        std::cerr << "Neural Ecology stopped cell recall did not reach its stored climate\n";
        return false;
    }

    lattice.setStorage(storage);
    std::array<float, s3g::kAmbiNeuralEcologyPickups> pickups {};
    pickups[1u] = 1.0f; // Four-pickup vocabulary maps pickup 1 to east.
    lattice.advance(0.20f, pickups, 4u, s3g::AmbiNeuralScoreMode::Field, 0.25f, 0.10f);
    lattice.advance(0.20f, pickups, 4u, s3g::AmbiNeuralScoreMode::Field, 0.25f, 0.10f);
    lattice.advance(0.20f, pickups, 4u, s3g::AmbiNeuralScoreMode::Field, 0.25f, 0.10f);
    if (lattice.currentCell() != 6u) {
        std::cerr << "Neural Ecology pickup vote did not move the Field Lattice east\n";
        return false;
    }

    auto base = s3g::ambiNeuralEcologyFactoryPreset(0u);
    const auto inscription = s3g::inscribeAmbiNeuralLatticeCell(base);
    const auto unchanged = s3g::applyAmbiNeuralLatticeCell(base, inscription, 0.0f);
    const auto changed = s3g::applyAmbiNeuralLatticeCell(base, storage.cells[15u], 1.0f);
    if (unchanged.activity != base.activity || unchanged.fieldReturn != base.fieldReturn
        || unchanged.mobility != base.mobility) {
        std::cerr << "Neural Ecology zero lattice amount changed the base ecology\n";
        return false;
    }
    const double climateDifference = std::fabs(changed.activity - base.activity)
        + std::fabs(changed.ringFeedback - base.ringFeedback)
        + std::fabs(changed.fieldReturn - base.fieldReturn)
        + std::fabs(changed.mobility - base.mobility);
    if (!(climateDifference > 0.1)) {
        std::cerr << "Neural Ecology lattice climate did not alter its conditions\n";
        return false;
    }
    return true;
}

bool testSanitization()
{
    s3g::AmbiNeuralEcologyParams unsafe;
    unsafe.order = 99u;
    unsafe.nodeSet = static_cast<s3g::AmbiNeuralNodeSet>(99u);
    unsafe.drive = 99.0f;
    unsafe.propagationMs = -4.0f;
    unsafe.plasticityMode = static_cast<s3g::AmbiNeuralPlasticityMode>(99u);
    unsafe.listeningMode = static_cast<s3g::AmbiNeuralListeningMode>(99u);
    unsafe.pickupSet = static_cast<s3g::AmbiNeuralPickupSet>(99u);
    unsafe.pickupAdapt = 4.0f;
    unsafe.pickupAnchor = -1.0f;
    unsafe.auditoryPlasticity = -2.0f;
    unsafe.metabolism = 8.0f;
    unsafe.adaptation = -4.0f;
    unsafe.genomeMorph = 3.0f;
    unsafe.heredity = -1.0f;
    unsafe.mutationDepth = 4.0f;
    unsafe.scoreMode = static_cast<s3g::AmbiNeuralScoreMode>(99u);
    unsafe.scorePlanes = static_cast<s3g::AmbiNeuralScorePlanes>(99u);
    unsafe.scoreAmount = 4.0f;
    unsafe.scoreDwellSeconds = -1.0f;
    unsafe.scoreTransitionSeconds = 99.0f;
    unsafe.centerDistance = 99.0f;
    unsafe.seed = 0u;
    const auto safe = s3g::sanitizeAmbiNeuralEcologyParams(unsafe);
    return safe.order == 7u && safe.nodeSet == s3g::AmbiNeuralNodeSet::Field64
        && safe.drive == 5.0f && safe.propagationMs == 0.0f
        && safe.plasticityMode == s3g::AmbiNeuralPlasticityMode::Prune
        && safe.listeningMode == s3g::AmbiNeuralListeningMode::Roaming
        && safe.pickupSet == s3g::AmbiNeuralPickupSet::Cube8
        && safe.pickupAdapt == 1.0f && safe.pickupAnchor == 0.0f
        && safe.auditoryPlasticity == 0.0f && safe.metabolism == 1.0f && safe.adaptation == 0.0f
        && safe.genomeMorph == 1.0f && safe.heredity == 0.0f && safe.mutationDepth == 1.0f
        && safe.scoreMode == s3g::AmbiNeuralScoreMode::Coupled && safe.scoreAmount == 1.0f
        && safe.scorePlanes == s3g::AmbiNeuralScorePlanes::Eight
        && safe.scoreDwellSeconds == 0.25f && safe.scoreTransitionSeconds == 30.0f
        && safe.centerDistance == 8.0f && safe.seed == 1u;
}

} // namespace

int main()
{
    if (!testSanitization()) return 1;
    if (!testPresets()) return 1;
    if (!testRandomizedAudibility()) return 1;
    if (!testNodeSetsAndRouting()) return 1;
    if (!testFieldListening()) return 1;
    if (!testAdaptiveListening()) return 1;
    if (!testDeterminismAndMutation()) return 1;
    if (!testPickupSetsAndGenomes()) return 1;
    if (!testFieldLattice()) return 1;
    if (!testSeventhOrderEnergy()) return 1;
    std::cout << "s3g Ambi Neural Ecology smoke test passed\n";
    return 0;
}
