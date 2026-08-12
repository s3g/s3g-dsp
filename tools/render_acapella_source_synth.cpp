#include "s3g_acapella_source_synth.h"
#include "s3g_acapella_ensemble_synth.h"
#include "s3g_acapella_text_compiler.h"
#include "s3g_acapella_vocal_fx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void writeU16(std::ofstream& stream, uint16_t value)
{
    const std::array<char, 2u> bytes {{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
    }};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ofstream& stream, uint32_t value)
{
    const std::array<char, 4u> bytes {{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu),
    }};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool writeMonoWav(const std::string& path, const std::vector<float>& audio,
    uint32_t sampleRate)
{
    if (audio.empty() || audio.size() > 0x7fffffffu) return false;
    const uint32_t dataBytes = static_cast<uint32_t>(audio.size() * 2u);
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.write("RIFF", 4);
    writeU32(stream, 36u + dataBytes);
    stream.write("WAVEfmt ", 8);
    writeU32(stream, 16u);
    writeU16(stream, 1u);
    writeU16(stream, 1u);
    writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * 2u);
    writeU16(stream, 2u);
    writeU16(stream, 16u);
    stream.write("data", 4);
    writeU32(stream, dataBytes);
    for (const float value : audio) {
        const float bounded = std::clamp(value, -1.0f, 1.0f);
        const auto sample = static_cast<int16_t>(std::lround(bounded * 32767.0f));
        writeU16(stream, static_cast<uint16_t>(sample));
    }
    return static_cast<bool>(stream);
}

bool parsePreset(const std::string& name, s3g::AcapellaSourcePreset& preset)
{
    if (name == "neutral") {
        preset = s3g::AcapellaSourcePreset::NeutralSung;
    } else if (name == "rhythmic") {
        preset = s3g::AcapellaSourcePreset::RhythmicRap;
    } else if (name == "air") {
        preset = s3g::AcapellaSourcePreset::AirySung;
    } else if (name == "pressed") {
        preset = s3g::AcapellaSourcePreset::PressedLead;
    } else if (name == "overdrive") {
        preset = s3g::AcapellaSourcePreset::HarshScream;
    } else if (name == "subharmonic") {
        preset = s3g::AcapellaSourcePreset::DeathGrowl;
    } else {
        return false;
    }
    return true;
}

bool parsePvocProfile(const std::string& name, uint32_t& profile)
{
    if (name == "vowel-suspension") profile = 6u;
    else if (name == "reverse-breath") profile = 7u;
    else if (name == "formant-loom") profile = 8u;
    else if (name == "partial-rain") profile = 9u;
    else if (name == "phase-choir") profile = 10u;
    else if (name == "consonant-shadow") profile = 11u;
    else if (name == "time-scar") profile = 12u;
    else if (name == "chord-glass") profile = 13u;
    else return false;
    return true;
}

struct Gesture {
    float startSeconds;
    float holdSeconds;
    float frequencyHz;
    s3g::AcapellaVowel vowel;
    s3g::AcapellaOnset onset;
    float velocity;
};

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cout << "Usage: s3g_acapella_source_synth_render OUTPUT.wav "
                     "[neutral|rhythmic|air|pressed|overdrive|subharmonic] "
                     "[poly] [sequence] [sync|transport] [freeze|scrub|cloud] "
                     "[text=your phrase] [pvoc-profile=time-scar]\n";
        return 1;
    }
    s3g::AcapellaSourcePreset preset = s3g::AcapellaSourcePreset::NeutralSung;
    if (argc >= 3 && !parsePreset(argv[2], preset)) {
        std::cerr << "Preset must be neutral, rhythmic, air, pressed, "
                     "overdrive, or subharmonic.\n";
        return 1;
    }
    bool polyphonicDemo = false;
    bool sequenceDemo = false;
    bool tempoSync = false;
    bool transportSync = false;
    bool freezeDemo = false;
    bool cloudDemo = false;
    bool scrubDemo = false;
    uint32_t pvocProfile = 0u;
    std::string textPhrase;
    for (int index = 3; index < argc; ++index) {
        const std::string option(argv[index]);
        polyphonicDemo = polyphonicDemo || option == "poly";
        sequenceDemo = sequenceDemo || option == "sequence";
        tempoSync = tempoSync || option == "sync" || option == "transport";
        transportSync = transportSync || option == "transport";
        freezeDemo = freezeDemo || option == "freeze";
        cloudDemo = cloudDemo || option == "cloud";
        scrubDemo = scrubDemo || option == "scrub";
        if (option.rfind("text=", 0u) == 0u) {
            textPhrase = option.substr(5u);
            sequenceDemo = true;
        }
        if (option.rfind("pvoc-profile=", 0u) == 0u
            && !parsePvocProfile(option.substr(13u), pvocProfile)) {
            std::cerr << "Unknown PVOC profile: " << option.substr(13u) << '\n';
            return 1;
        }
    }
    if (pvocProfile != 0u) preset = s3g::acapellaPvocProfileBase(pvocProfile);

    constexpr uint32_t sampleRate = 48000u;
    constexpr float durationSeconds = 4.25f;
    std::array<Gesture, 8u> gestures {{
        { 0.10f, 0.36f, 146.83f, s3g::AcapellaVowel::Schwa,
            s3g::AcapellaOnset::T, 0.82f },
        { 0.58f, 0.42f, 164.81f, s3g::AcapellaVowel::A,
            s3g::AcapellaOnset::M, 0.76f },
        { 1.13f, 0.35f, 174.61f, s3g::AcapellaVowel::I,
            s3g::AcapellaOnset::S, 0.88f },
        { 1.61f, 0.52f, 196.00f, s3g::AcapellaVowel::O,
            s3g::AcapellaOnset::R, 0.80f },
        { 2.27f, 0.29f, 174.61f, s3g::AcapellaVowel::U,
            s3g::AcapellaOnset::K, 0.92f },
        { 2.69f, 0.34f, 164.81f, s3g::AcapellaVowel::E,
            s3g::AcapellaOnset::V, 0.74f },
        { 3.16f, 0.28f, 146.83f, s3g::AcapellaVowel::Schwa,
            s3g::AcapellaOnset::N, 0.86f },
        { 3.57f, 0.48f, 130.81f, s3g::AcapellaVowel::A,
            s3g::AcapellaOnset::Sh, 0.78f },
    }};
    uint32_t gestureCount = static_cast<uint32_t>(gestures.size());
    if (sequenceDemo) {
        gestures[0] = { 0.10f, 3.72f, 98.0f, s3g::AcapellaVowel::O,
            s3g::AcapellaOnset::H, 0.86f };
        gestureCount = 1u;
    }

    const uint32_t frames = static_cast<uint32_t>(durationSeconds * sampleRate);
    std::vector<float> audio(frames, 0.0f);
    s3g::AcapellaEnsembleSynth ensemble;
    auto sourceParams = s3g::acapellaSourcePreset(preset);
    if (!textPhrase.empty()) {
        sourceParams.gestureSequence = s3g::AcapellaGestureSequence::Text;
    } else if (sequenceDemo) {
        switch (preset) {
        case s3g::AcapellaSourcePreset::RhythmicRap:
            sourceParams.gestureSequence = s3g::AcapellaGestureSequence::RapGrid;
            sourceParams.gestureRateHz = 8.5f;
            break;
        case s3g::AcapellaSourcePreset::HarshScream:
            sourceParams.gestureSequence = s3g::AcapellaGestureSequence::ScreamArc;
            sourceParams.gestureRateHz = 6.5f;
            break;
        case s3g::AcapellaSourcePreset::DeathGrowl:
            sourceParams.gestureSequence = s3g::AcapellaGestureSequence::DeathChant;
            sourceParams.gestureRateHz = 6.0f;
            break;
        default:
            sourceParams.gestureSequence = s3g::AcapellaGestureSequence::VowelOrbit;
            sourceParams.gestureRateHz = 5.5f;
            break;
        }
        sourceParams.gestureDepth = 1.0f;
        sourceParams.gestureLoop = true;
    }
    if (tempoSync) {
        sourceParams.gestureSync = transportSync
            ? s3g::AcapellaGestureSync::Transport
            : s3g::AcapellaGestureSync::Note;
        sourceParams.gestureDivision = s3g::AcapellaGestureDivision::Eighth;
    }
    ensemble.setSourceParams(sourceParams);
    ensemble.setParams(s3g::acapellaEnsemblePreset(preset));
    if (!textPhrase.empty()) {
        const auto compiled = s3g::compileAcapellaText(textPhrase.c_str());
        ensemble.setTextGestureProgram(compiled.program);
        std::cout << "Compiled " << compiled.program.wordCount << " words into "
                  << compiled.program.count << " phoneme events\n";
    }
    ensemble.prepare(sampleRate);
    ensemble.setGestureTransport(120.0, 0.0, true, true, true);
    s3g::AcapellaVocalEffects effects;
    auto effectParams = s3g::acapellaVocalFxPreset(preset);
    effectParams.intelligibility = sourceParams.intelligibility;
    if (pvocProfile != 0u) {
        effectParams = s3g::acapellaPvocProfileEffects(
            pvocProfile, effectParams);
    }
    if (freezeDemo) {
        effectParams.pvoc.amount = 0.90f;
        effectParams.pvoc.mode = s3g::AcapellaPvocMode::Freeze;
        effectParams.pvoc.memoryMs = 3000.0f;
        effectParams.pvoc.captureTrigger =
            s3g::AcapellaPvocCaptureTrigger::Word;
        effectParams.pvoc.captureReleaseMs = 900.0f;
    }
    if (cloudDemo) {
        effectParams.pvoc.amount = 1.0f;
        effectParams.pvoc.mode = s3g::AcapellaPvocMode::Cloud;
        effectParams.pvoc.memoryMs = 4200.0f;
        effectParams.pvoc.position = 0.32f;
        effectParams.pvoc.speed = 0.72f;
        effectParams.pvoc.timeSpread = 0.64f;
        effectParams.pvoc.heads = 5u;
        effectParams.pvoc.partialCloud = 0.58f;
        effectParams.pvoc.phaseMode = s3g::AcapellaPvocPhaseMode::Loose;
        effectParams.pvoc.coherence = 0.42f;
        effectParams.pvoc.phaseDrift = 0.46f;
    }
    if (scrubDemo && !cloudDemo) {
        effectParams.pvoc.amount = 1.0f;
        effectParams.pvoc.mode = s3g::AcapellaPvocMode::Scrub;
        effectParams.pvoc.memoryMs = 2800.0f;
        effectParams.pvoc.position = 0.42f;
        effectParams.pvoc.speed = 0.82f;
        effectParams.pvoc.timeSpread = 0.74f;
        effectParams.pvoc.heads = 3u;
        effectParams.pvoc.coherence = 0.78f;
    }
    effects.setParams(effectParams);
    effects.prepare(sampleRate);
    effects.setTempo(120.0, true);

    uint32_t nextGesture = 0u;
    uint32_t releaseFrame = 0u;
    int32_t activeNoteId = -1;
    int16_t activeKey = -1;
    int32_t harmonyNoteId = -1;
    int16_t harmonyKey = -1;
    bool gate = false;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (nextGesture < gestureCount) {
            const Gesture& gesture = gestures[nextGesture];
            const uint32_t startFrame = static_cast<uint32_t>(
                gesture.startSeconds * sampleRate);
            if (frame == startFrame) {
                activeNoteId = static_cast<int32_t>(nextGesture);
                activeKey = static_cast<int16_t>(60u + nextGesture);
                ensemble.trigger({
                    { gesture.vowel, gesture.onset, gesture.frequencyHz,
                        gesture.velocity, gesture.holdSeconds * 1000.0f },
                    activeNoteId, 0, activeKey,
                });
                if (polyphonicDemo) {
                    harmonyNoteId = activeNoteId + 100;
                    harmonyKey = activeKey + 24;
                    ensemble.trigger({
                        { gesture.vowel, gesture.onset,
                            gesture.frequencyHz * 1.5f,
                            gesture.velocity * 0.58f,
                            gesture.holdSeconds * 1000.0f },
                        harmonyNoteId, 0, harmonyKey,
                    });
                }
                releaseFrame = frame + static_cast<uint32_t>(
                    gesture.holdSeconds * sampleRate);
                gate = true;
                ++nextGesture;
            }
        }
        if (gate && frame == releaseFrame) {
            ensemble.release(activeNoteId, 0, activeKey);
            if (polyphonicDemo) {
                ensemble.release(harmonyNoteId, 0, harmonyKey);
            }
            gate = false;
        }
        const auto voices = ensemble.processFrame();
        effects.setPvocGesture(ensemble.pvocGesture());
        const auto processed = effects.processFrameStereo(
            voices.left, voices.right);
        audio[frame] = 0.5f * (processed.left + processed.right);
    }

    if (!writeMonoWav(argv[1], audio, sampleRate)) {
        std::cerr << "Could not write " << argv[1] << '\n';
        return 1;
    }
    std::cout << "Rendered sample-free articulator to " << argv[1] << '\n';
    return 0;
}
