#include "s3g_processor_lowform.h"
#include "s3g_processor_lowform_presets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

constexpr double kSampleRate = 48000.0;

bool idleAndSanitizationProbe()
{
    s3g::Lowform synth;
    synth.prepare(std::numeric_limits<double>::quiet_NaN());
    for (uint32_t i = 0u; i < 2048u; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        synth.processFrame(left, right);
        if (left != 0.0f || right != 0.0f
            || !std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "idle Processor Lowform was not finite silence\n";
            return false;
        }
    }
    auto invalid = synth.params();
    invalid.outputGainDb = std::numeric_limits<float>::quiet_NaN();
    invalid.bodyEngine = 99.0f;
    invalid.cutoffHz = std::numeric_limits<float>::infinity();
    invalid.filterType = std::numeric_limits<float>::quiet_NaN();
    invalid.pitchPunchSemitones = std::numeric_limits<float>::quiet_NaN();
    invalid.bodyControlC = -3.0f;
    invalid.bodyControlD = std::numeric_limits<float>::infinity();
    invalid.bodyControlE = 4.0f;
    invalid.textureWidth = std::numeric_limits<float>::quiet_NaN();
    invalid.modalDrive = -2.0f;
    invalid.modWheelAmount = std::numeric_limits<float>::infinity();
    invalid.dynamicsBite = 3.0f;
    invalid.mods[0].depth = -20.0f;
    invalid.mods[0].secondaryDepth = 20.0f;
    invalid.mods[0].secondaryTarget = 99.0f;
    invalid.expressionRoutes[0].depth =
        std::numeric_limits<float>::infinity();
    invalid.expressionRoutes[1].target = 99.0f;
    invalid.bodyDecaySeconds = -1.0f;
    invalid.textureAttackSeconds = 8.0f;
    invalid.textureDecaySeconds =
        std::numeric_limits<float>::quiet_NaN();
    invalid.shredCircuit = 99.0f;
    invalid.tilt = 4.0f;
    synth.setParams(invalid);
    const auto& p = synth.params();
    const bool sanitized = p.outputGainDb == -12.0f && p.bodyEngine == 7.0f
        && p.cutoffHz == 820.0f && p.mods[0].depth == -1.0f
        && p.filterType == 2.0f && p.pitchPunchSemitones == 3.0f
        && p.bodyControlC == 0.0f && p.bodyControlD == 0.24f
        && p.bodyControlE == 1.0f
        && p.textureWidth == 0.47f && p.modalDrive == 0.0f
        && p.modWheelAmount == 0.75f && p.dynamicsBite == 1.0f
        && p.mods[0].secondaryDepth == 1.0f
        && p.mods[0].secondaryTarget == 28.0f
        && p.expressionRoutes[0].depth == 0.0f
        && p.expressionRoutes[1].target == 28.0f
        && p.bodyDecaySeconds == 0.02f
        && p.textureAttackSeconds == 2.0f
        && p.textureDecaySeconds == 12.0f
        && p.shredCircuit == 7.0f && p.tilt == 1.0f;
    if (!sanitized)
        std::cerr << "Processor Lowform parameter sanitization failed\n";
    return sanitized;
}

bool transientSafeBodyOnsetProbe()
{
    constexpr s3g::BassBodyEngine engines[] {
        s3g::BassBodyEngine::Modal,
        s3g::BassBodyEngine::Acid,
        s3g::BassBodyEngine::Rave,
        s3g::BassBodyEngine::Phase,
        s3g::BassBodyEngine::Throat,
        s3g::BassBodyEngine::Sync,
    };
    for (const auto engine : engines) {
        s3g::Lowform synth;
        synth.prepare(kSampleRate);
        auto p = synth.params();
        p.outputGainDb = 0.0f;
        p.foundationLevel = 0.0f;
        p.bodyEngine = static_cast<float>(engine);
        p.bodyLevel = 1.0f;
        p.bodyControlA = 0.58f;
        p.bodyControlB = 0.82f;
        p.bodyControlC = 0.72f;
        p.bodyControlD = 0.46f;
        p.bodyControlE = 0.60f;
        p.textureMode = static_cast<float>(s3g::BassTextureMode::Off);
        p.filterType = static_cast<float>(s3g::BassFilterType::Bypass);
        p.bodyFilter = 0.0f;
        p.attackSeconds = 0.0005f;
        p.pitchPunchSemitones = 0.0f;
        p.tube = 0.0f;
        p.shred = 0.0f;
        p.shredFeedback = 0.0f;
        p.shredMix = 0.0f;
        p.dynamics = 0.0f;
        p.saturation = 0.0f;
        p.clip = 0.0f;
        p.maximizer = 0.0f;
        p.modalDrive = 0.0f;
        p.dynamicsBite = 0.0f;
        synth.setParams(p);
        synth.noteOn(36, 1.0f, 1, 0);

        float previous = 0.0f;
        float earlyPeak = 0.0f;
        float maximumDelta = 0.0f;
        double settledEnergy = 0.0;
        for (uint32_t sample = 0u; sample < 512u; ++sample) {
            float left = 0.0f;
            float right = 0.0f;
            synth.processFrame(left, right);
            const float mono = 0.5f * (left + right);
            if (!std::isfinite(mono)) return false;
            if (sample < 16u)
                earlyPeak = std::max(earlyPeak, std::fabs(mono));
            if (sample < 256u)
                maximumDelta = std::max(maximumDelta,
                    std::fabs(mono - previous));
            else
                settledEnergy += static_cast<double>(mono) * mono;
            previous = mono;
        }
        const float deltaLimit = engine == s3g::BassBodyEngine::Phase
                || engine == s3g::BassBodyEngine::Sync
            ? 0.55f : 0.08f;
        if (earlyPeak > 0.08f || maximumDelta > deltaLimit
            || settledEnergy < 1.0e-5) {
            std::cerr << "transient-safe body onset failed for engine "
                      << static_cast<uint32_t>(engine)
                      << " (early=" << earlyPeak
                      << ", delta=" << maximumDelta << ")\n";
            return false;
        }
    }
    return true;
}

bool factoryPresetProbe()
{
    for (uint32_t preset = 0u;
         preset < s3g::kLowformFactoryPresetCount; ++preset) {
        s3g::Lowform synth;
        synth.prepare(kSampleRate);
        synth.setParams(s3g::lowformFactoryPreset(preset));
        synth.beginBlock();
        synth.noteOn(33 + static_cast<int>(preset % 12u), 0.82f,
            static_cast<int32_t>(preset), 0);
        double energy = 0.0;
        float peak = 0.0f;
        for (uint32_t sample = 0u; sample < 18000u; ++sample) {
            synth.setTransport(static_cast<double>(sample) / 12000.0,
                120.0, true);
            float left = 0.0f;
            float right = 0.0f;
            synth.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)
                || std::fabs(left) > 2.001f || std::fabs(right) > 2.001f) {
                std::cerr << "preset " << preset
                          << " produced invalid output\n";
                return false;
            }
            energy += static_cast<double>(left) * left
                + static_cast<double>(right) * right;
            peak = std::max(peak, std::max(std::fabs(left), std::fabs(right)));
        }
        if (energy < 0.01 || peak < 0.002f) {
            std::cerr << "preset " << preset << " was unexpectedly silent\n";
            return false;
        }
    }
    return true;
}

bool polyphonyAndExpressionProbe()
{
    s3g::Lowform synth;
    synth.prepare(kSampleRate);
    auto p = synth.params();
    p.voiceMode = static_cast<float>(s3g::BassVoiceMode::Poly);
    p.bodyEngine = static_cast<float>(s3g::BassBodyEngine::Swarm);
    p.textureMode = static_cast<float>(s3g::BassTextureMode::Wire);
    p.textureLevel = 0.30f;
    p.shred = 0.42f;
    p.shredFeedback = 0.12f;
    p.shredMix = 0.34f;
    p.releaseSeconds = 0.06f;
    synth.setParams(p);
    for (int voice = 0; voice < 10; ++voice) {
        synth.noteOn(28 + voice * 2, 0.7f,
            100 + voice, static_cast<int16_t>(voice & 1));
    }
    synth.setPressure(-1, 104, -1, 0.9f);
    synth.setTimbre(-1, 105, -1, 0.8f);
    synth.setTuning(-1, 106, -1, 0.25f);
    synth.setPitchBend(1.0f);
    synth.setModWheel(0.7f);
    double energy = 0.0;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return false;
        energy += static_cast<double>(left) * left
            + static_cast<double>(right) * right;
    }
    synth.allNotesOff();
    for (uint32_t sample = 0u; sample < 48000u && synth.active(); ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
    }
    if (energy < 0.05 || synth.active()) {
        std::cerr << "polyphony/expression or tail lifecycle failed\n";
        return false;
    }
    return true;
}

double channelExpressionSignature(uint32_t expression, int16_t channel)
{
    s3g::Lowform synth;
    synth.prepare(kSampleRate);
    auto p = synth.params();
    p.outputGainDb = -12.0f;
    p.foundationLevel = 0.35f;
    p.bodyEngine = static_cast<float>(s3g::BassBodyEngine::Pressure);
    p.bodyLevel = 0.82f;
    p.textureMode = static_cast<float>(s3g::BassTextureMode::Off);
    p.filterType = static_cast<float>(s3g::BassFilterType::Bypass);
    p.foundationFilter = p.bodyFilter = 0.0f;
    p.pitchPunchSemitones = 0.0f;
    p.tube = p.shred = p.shredMix = p.dynamics = 0.0f;
    p.saturation = p.clip = p.maximizer = 0.0f;
    p.dynamicsBite = 0.0f;
    synth.setParams(p);
    synth.noteOn(42, 0.82f, 11, 2);
    if (expression == 1u)
        synth.setTuning(-1, -1, channel, 7.0f);
    else if (expression == 2u)
        synth.setPressure(-1, -1, channel, 0.92f);
    else if (expression == 3u)
        synth.setTimbre(-1, -1, channel, 0.90f);

    double signature = 0.0;
    for (uint32_t sample = 0u; sample < 8192u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return -1.0;
        signature += (1.0 + static_cast<double>(sample % 19u) * 0.003)
            * (std::fabs(left) + 1.11 * std::fabs(right));
    }
    return signature;
}

bool mpeChannelIsolationProbe()
{
    const double baseline = channelExpressionSignature(0u, 2);
    if (baseline <= 0.0) return false;
    for (uint32_t expression = 1u; expression <= 3u; ++expression) {
        const double wrongChannel = channelExpressionSignature(expression, 3);
        const double memberChannel = channelExpressionSignature(expression, 2);
        if (std::fabs(wrongChannel - baseline) > 1.0e-9
            || std::fabs(memberChannel - baseline) < 1.0e-3) {
            std::cerr << "MPE channel isolation failed for expression "
                      << expression << '\n';
            return false;
        }
    }
    return true;
}

double directControlSignature(uint32_t engine, uint32_t control,
    float value)
{
    s3g::Lowform synth;
    synth.prepare(kSampleRate);
    auto p = synth.params();
    p.outputGainDb = -12.0f;
    p.foundationLevel = 0.0f;
    p.bodyEngine = static_cast<float>(engine);
    p.bodyLevel = 0.82f;
    p.bodyControlA = 0.15f;
    p.bodyControlB = 0.15f;
    p.bodyControlC = 0.15f;
    p.bodyControlD = 0.15f;
    p.bodyControlE = 0.15f;
    if (control == 0u) p.bodyControlA = value;
    else if (control == 1u) p.bodyControlB = value;
    else if (control == 2u) p.bodyControlC = value;
    else if (control == 3u) p.bodyControlD = value;
    else p.bodyControlE = value;
    p.textureMode = static_cast<float>(s3g::BassTextureMode::Off);
    p.filterType = static_cast<float>(s3g::BassFilterType::Bypass);
    p.bodyFilter = 0.0f;
    p.tube = p.shred = p.shredMix = p.dynamics = 0.0f;
    p.saturation = p.clip = p.maximizer = 0.0f;
    p.modalDrive = 0.0f;
    p.dynamicsBite = 0.0f;
    synth.setParams(p);
    synth.noteOn(36, 0.82f, 1, 0);
    double signature = 0.0;
    for (uint32_t sample = 0u; sample < 8192u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return -1.0;
        const double weight = 1.0 + static_cast<double>(sample % 17u) * 0.01;
        signature += weight * (std::fabs(left) + 1.07 * std::fabs(right));
    }
    return signature;
}

bool directBodyControlProbe()
{
    for (uint32_t engine = 0u; engine < 8u; ++engine) {
        for (uint32_t control = 0u; control < 5u; ++control) {
            const double low = directControlSignature(engine, control, 0.15f);
            const double high = directControlSignature(engine, control, 0.85f);
            if (low < 0.0 || high < 0.0 || std::fabs(high - low) < 1.0e-3) {
                std::cerr << "body engine " << engine << " control "
                          << control << " had no measurable effect\n";
                return false;
            }
        }
    }
    return true;
}

double modalVelocityEnergy(float velocity)
{
    s3g::Lowform synth;
    synth.prepare(kSampleRate);
    auto p = synth.params();
    p.outputGainDb = -12.0f;
    p.foundationLevel = 0.0f;
    p.bodyEngine = static_cast<float>(s3g::BassBodyEngine::Modal);
    p.bodyLevel = 0.82f;
    p.bodyControlA = 0.55f;
    p.bodyControlB = 0.42f;
    p.bodyControlC = 0.52f;
    p.bodyControlD = 0.0f;
    p.bodyControlE = 0.56f;
    p.filterType = static_cast<float>(s3g::BassFilterType::Bypass);
    p.bodyFilter = 0.0f;
    p.pitchPunchSemitones = 0.0f;
    p.tube = p.shred = p.shredMix = p.dynamics = 0.0f;
    p.saturation = p.clip = p.maximizer = 0.0f;
    p.modalDrive = 0.0f;
    p.dynamicsBite = 0.0f;
    synth.setParams(p);
    synth.noteOn(36, velocity, 1, 0);
    double energy = 0.0;
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return -1.0;
        energy += static_cast<double>(left) * left
            + static_cast<double>(right) * right;
    }
    return energy;
}

bool modalRobustnessProbe()
{
    const double softEnergy = modalVelocityEnergy(0.18f);
    const double hardEnergy = modalVelocityEnergy(0.95f);
    if (softEnergy <= 0.0 || hardEnergy < softEnergy * 2.0) {
        std::cerr << "modal excitation did not follow velocity\n";
        return false;
    }

    s3g::Lowform synth;
    synth.prepare(kSampleRate);
    auto p = synth.params();
    p.outputGainDb = -9.0f;
    p.foundationLevel = 0.0f;
    p.bodyEngine = static_cast<float>(s3g::BassBodyEngine::Modal);
    p.bodyLevel = 1.0f;
    p.bodyControlD = 0.72f;
    p.filterType = static_cast<float>(s3g::BassFilterType::Bypass);
    p.bodyFilter = 0.0f;
    p.attackSeconds = 0.0005f;
    p.pitchPunchSemitones = 0.0f;
    p.tube = p.shred = p.shredMix = p.dynamics = 0.0f;
    p.saturation = p.clip = p.maximizer = 0.0f;
    p.modalDrive = 0.0f;
    p.dynamicsBite = 0.0f;
    synth.setParams(p);
    synth.noteOn(30, 1.0f, 1, 0);

    float previous = 0.0f;
    float maximumDelta = 0.0f;
    float peak = 0.0f;
    double lateEnergy = 0.0;
    for (uint32_t sample = 0u; sample < 30000u; ++sample) {
        if (sample > 0u && sample % 701u == 0u) {
            p.bodyControlA = p.bodyControlA < 0.5f ? 1.0f : 0.0f;
            p.bodyControlB = p.bodyControlB < 0.5f ? 1.0f : 0.0f;
            p.bodyControlC = p.bodyControlC < 0.5f ? 1.0f : 0.0f;
            p.bodyControlD = p.bodyControlD < 0.5f ? 1.0f : 0.0f;
            p.bodyControlE = p.bodyControlE < 0.5f ? 1.0f : 0.0f;
            synth.setParams(p);
        }
        if (sample == 9000u || sample == 18000u) {
            for (int note = 0; note < 9; ++note)
                synth.noteOn(30 + note, 0.65f + 0.03f * note,
                    100 + note, 0);
        }
        float left = 0.0f;
        float right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)
            || std::fabs(left) > 2.001f || std::fabs(right) > 2.001f) {
            std::cerr << "modal extreme automation became unstable\n";
            return false;
        }
        const float mono = 0.5f * (left + right);
        maximumDelta = std::max(maximumDelta, std::fabs(mono - previous));
        peak = std::max(peak, std::fabs(mono));
        if (sample > 24000u)
            lateEnergy += static_cast<double>(mono) * mono;
        previous = mono;
    }
    if (maximumDelta > 0.30f || peak < 0.01f || lateEnergy < 1.0e-4) {
        std::cerr << "modal continuity/sustain guard failed (delta="
                  << maximumDelta << ", peak=" << peak << ")\n";
        return false;
    }
    return true;
}

bool layerAndModulationProbe()
{
    double checksum = 0.0;
    for (uint32_t body = 0u; body < 8u; ++body) {
        for (uint32_t texture = 0u; texture < 5u; ++texture) {
            for (uint32_t filter = 0u; filter < 6u; ++filter) {
                s3g::Lowform synth;
                synth.prepare(kSampleRate);
                auto p = synth.params();
                p.bodyEngine = static_cast<float>(body);
                p.textureMode = static_cast<float>(texture);
                p.textureLevel = texture == 0u ? 0.0f : 0.38f;
                p.filterType = static_cast<float>(filter);
                p.mods[0] = { static_cast<float>(s3g::BassModShape::Sine),
                    0.4f, 0.6f,
                    static_cast<float>(s3g::BassModTarget::Cutoff) };
                p.mods[1] = { static_cast<float>(s3g::BassModShape::Steps),
                    0.5f, -0.4f,
                    static_cast<float>(s3g::BassModTarget::BodyMorph) };
                p.mods[2] = { static_cast<float>(s3g::BassModShape::Random),
                    0.7f, 0.3f,
                    static_cast<float>(s3g::BassModTarget::Width) };
                synth.setParams(p);
                synth.noteOn(36, 0.8f, 1, 0);
                for (uint32_t sample = 0u; sample < 1024u; ++sample) {
                    float left = 0.0f;
                    float right = 0.0f;
                    synth.processFrame(left, right);
                    if (!std::isfinite(left) || !std::isfinite(right)) {
                        std::cerr << "layer/modulation combination failed\n";
                        return false;
                    }
                    checksum += std::fabs(left) + std::fabs(right);
                }
            }
        }
    }
    return checksum > 1.0;
}

bool expandedModulationTargetProbe()
{
    constexpr std::array<s3g::BassModTarget, 19u> targets {{
        s3g::BassModTarget::FoundationLevel,
        s3g::BassModTarget::BodyLevel,
        s3g::BassModTarget::BodyControlC,
        s3g::BassModTarget::BodyControlD,
        s3g::BassModTarget::BodyControlE,
        s3g::BassModTarget::BodyWidth,
        s3g::BassModTarget::TextureTrack,
        s3g::BassModTarget::TextureWidth,
        s3g::BassModTarget::FilterDrive,
        s3g::BassModTarget::KeyTrack,
        s3g::BassModTarget::AmplifierTube,
        s3g::BassModTarget::ShredAmount,
        s3g::BassModTarget::ShredFeedback,
        s3g::BassModTarget::ShredColor,
        s3g::BassModTarget::ShredMix,
        s3g::BassModTarget::DynamicsAmount,
        s3g::BassModTarget::DynamicsBite,
        s3g::BassModTarget::DynamicsSaturation,
        s3g::BassModTarget::DynamicsTilt,
    }};
    for (const auto target : targets) {
        s3g::Lowform reference;
        s3g::Lowform modulated;
        reference.prepare(kSampleRate);
        modulated.prepare(kSampleRate);
        auto params = reference.params();
        params.outputGainDb = -6.0f;
        params.foundationLevel = 0.55f;
        params.bodyEngine = static_cast<float>(s3g::BassBodyEngine::Swarm);
        params.bodyLevel = 0.62f;
        params.bodyControlA = 0.35f;
        params.bodyControlB = 0.42f;
        params.bodyControlC = 0.58f;
        params.bodyControlD = 0.38f;
        params.bodyControlE = 0.52f;
        params.bodyWidth = 0.45f;
        params.textureMode = static_cast<float>(
            s3g::BassTextureMode::Corrode);
        params.textureLevel = 0.38f;
        params.textureColor = 0.47f;
        params.textureTrack = 0.52f;
        params.textureWidth = 0.44f;
        params.filterType = static_cast<float>(s3g::BassFilterType::Low24);
        params.cutoffHz = 780.0f;
        params.resonance = 0.28f;
        params.filterDrive = 0.26f;
        params.keyTrack = 0.35f;
        params.attackSeconds = 0.0005f;
        params.pitchPunchSemitones = 0.0f;
        params.tube = 0.12f;
        params.shred = 0.24f;
        params.shredFeedback = 0.12f;
        params.shredColor = 0.48f;
        params.shredMix = 0.34f;
        params.dynamics = 0.28f;
        params.dynamicsBite = 0.18f;
        params.saturation = 0.16f;
        params.clip = 0.0f;
        params.maximizer = 0.0f;
        for (auto& mod : params.mods) mod = {};
        params.mods[0] = {
            static_cast<float>(s3g::BassModShape::Square),
            0.50f, 0.80f, static_cast<float>(s3g::BassModTarget::Off)
        };
        reference.setParams(params);
        params.mods[0].target = static_cast<float>(target);
        modulated.setParams(params);
        reference.noteOn(48, 0.82f, 1, 0);
        modulated.noteOn(48, 0.82f, 1, 0);

        double difference = 0.0;
        for (uint32_t sample = 0u; sample < 8192u; ++sample) {
            float referenceLeft = 0.0f;
            float referenceRight = 0.0f;
            float modulatedLeft = 0.0f;
            float modulatedRight = 0.0f;
            reference.processFrame(referenceLeft, referenceRight);
            modulated.processFrame(modulatedLeft, modulatedRight);
            if (!std::isfinite(modulatedLeft)
                || !std::isfinite(modulatedRight)) {
                std::cerr << "expanded modulation target became non-finite\n";
                return false;
            }
            difference += std::fabs(modulatedLeft - referenceLeft)
                + std::fabs(modulatedRight - referenceRight);
        }
        if (difference < 1.0e-5) {
            std::cerr << "expanded modulation target had no effect: "
                      << static_cast<uint32_t>(target) << '\n';
            return false;
        }
    }
    return true;
}

bool secondaryAndActivityProbe()
{
    s3g::Lowform reference;
    s3g::Lowform routed;
    reference.prepare(kSampleRate);
    routed.prepare(kSampleRate);
    auto p = reference.params();
    p.outputGainDb = -9.0f;
    p.filterType = static_cast<float>(s3g::BassFilterType::Bypass);
    p.foundationFilter = p.bodyFilter = p.textureFilter = 0.0f;
    p.pitchPunchSemitones = 0.0f;
    p.tube = p.shred = p.shredMix = p.dynamics = 0.0f;
    p.saturation = p.clip = p.maximizer = 0.0f;
    for (auto& mod : p.mods) mod = {};
    p.mods[0].shape = static_cast<float>(s3g::BassModShape::Square);
    p.mods[0].rate = 0.5f;
    p.mods[0].secondaryDepth = 0.85f;
    reference.setParams(p);
    p.mods[0].secondaryTarget = static_cast<float>(
        s3g::BassModTarget::FoundationLevel);
    routed.setParams(p);
    reference.noteOn(42, 0.8f, 1, 0);
    routed.noteOn(42, 0.8f, 1, 0);
    double difference = 0.0;
    float activityPeak = 0.0f;
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
        reference.processFrame(aL, aR);
        routed.processFrame(bL, bR);
        difference += std::fabs(aL - bL) + std::fabs(aR - bR);
        activityPeak = std::max(activityPeak,
            std::fabs(routed.modActivity(0u)));
    }
    if (difference < 1.0e-4 || activityPeak < 0.9f) {
        std::cerr << "secondary modulation/activity probe failed\n";
        return false;
    }
    return true;
}

double pressureRouteSignature(bool routed, float pressure)
{
    s3g::Lowform synth;
    synth.prepare(kSampleRate);
    auto p = synth.params();
    p.outputGainDb = -9.0f;
    p.bodyEngine = static_cast<float>(s3g::BassBodyEngine::Pressure);
    p.textureMode = static_cast<float>(s3g::BassTextureMode::Off);
    p.filterType = static_cast<float>(s3g::BassFilterType::Low24);
    p.pitchPunchSemitones = 0.0f;
    p.tube = p.shred = p.shredMix = p.dynamics = 0.0f;
    p.saturation = p.clip = p.maximizer = 0.0f;
    for (auto& route : p.expressionRoutes) route = {};
    if (routed) {
        p.expressionRoutes[1].target = static_cast<float>(
            s3g::BassModTarget::Cutoff);
        p.expressionRoutes[1].depth = 0.85f;
    }
    synth.setParams(p);
    synth.noteOn(42, 0.8f, 7, 2);
    synth.setPressure(42, 7, 2, pressure);
    double signature = 0.0;
    for (uint32_t sample = 0u; sample < 8192u; ++sample) {
        float left = 0.0f, right = 0.0f;
        synth.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) return -1.0;
        signature += (1.0 + static_cast<double>(sample % 23u) * 0.01)
            * (std::fabs(left) + std::fabs(right));
    }
    return signature;
}

bool explicitExpressionRoutingProbe()
{
    const double hiddenOff = pressureRouteSignature(false, 0.0f);
    const double hiddenHigh = pressureRouteSignature(false, 0.95f);
    const double routedOff = pressureRouteSignature(true, 0.0f);
    const double routedHigh = pressureRouteSignature(true, 0.95f);
    if (hiddenOff <= 0.0 || routedOff <= 0.0
        || std::fabs(hiddenOff - hiddenHigh) > 1.0e-9
        || std::fabs(routedOff - routedHigh) < 1.0e-3) {
        std::cerr << "explicit expression routing probe failed\n";
        return false;
    }
    return true;
}

double layerLateEnergy(float bodyDecay, float textureAttack,
    float textureDecay, bool textureOnly, bool early)
{
    s3g::Lowform synth;
    synth.prepare(kSampleRate);
    auto p = synth.params();
    p.outputGainDb = -6.0f;
    p.foundationLevel = 0.0f;
    p.bodyLevel = textureOnly ? 0.0f : 0.9f;
    p.bodyEngine = static_cast<float>(s3g::BassBodyEngine::Pressure);
    p.textureMode = textureOnly
        ? static_cast<float>(s3g::BassTextureMode::Noise)
        : static_cast<float>(s3g::BassTextureMode::Off);
    p.textureLevel = textureOnly ? 0.8f : 0.0f;
    p.filterType = static_cast<float>(s3g::BassFilterType::Bypass);
    p.foundationFilter = p.bodyFilter = p.textureFilter = 0.0f;
    p.attackSeconds = 0.0005f;
    p.sustain = 1.0f;
    p.pitchPunchSemitones = 0.0f;
    p.bodyDecaySeconds = bodyDecay;
    p.textureAttackSeconds = textureAttack;
    p.textureDecaySeconds = textureDecay;
    p.tube = p.shred = p.shredMix = p.dynamics = 0.0f;
    p.saturation = p.clip = p.maximizer = 0.0f;
    synth.setParams(p);
    synth.noteOn(42, 0.8f, 1, 0);
    double energy = 0.0;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        float left = 0.0f, right = 0.0f;
        synth.processFrame(left, right);
        const bool window = early ? sample < 1024u : sample >= 20000u;
        if (window) energy += static_cast<double>(left) * left
            + static_cast<double>(right) * right;
    }
    return energy;
}

bool layerArticulationProbe()
{
    const double heldBody = layerLateEnergy(12.0f, 0.0f, 12.0f,
        false, false);
    const double shortBody = layerLateEnergy(0.06f, 0.0f, 12.0f,
        false, false);
    const double instantTexture = layerLateEnergy(12.0f, 0.0f, 12.0f,
        true, true);
    const double slowTexture = layerLateEnergy(12.0f, 0.6f, 12.0f,
        true, true);
    if (heldBody <= 0.0 || shortBody >= heldBody * 0.20
        || instantTexture <= 0.0 || slowTexture >= instantTexture * 0.45) {
        std::cerr << "layer articulation probe failed\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!idleAndSanitizationProbe()
        || !transientSafeBodyOnsetProbe()
        || !factoryPresetProbe()
        || !polyphonyAndExpressionProbe()
        || !mpeChannelIsolationProbe()
        || !directBodyControlProbe()
        || !modalRobustnessProbe()
        || !layerAndModulationProbe()
        || !expandedModulationTargetProbe()
        || !secondaryAndActivityProbe()
        || !explicitExpressionRoutingProbe()
        || !layerArticulationProbe()) return 1;
    std::cout << "Processor Lowform smoke passed\n";
    return 0;
}
