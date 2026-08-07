#include "s3g/tracker/instrument_rack.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace s3g::tracker {
namespace {

constexpr std::array<RackInstrument, kInstrumentRackSlotCount>
    kDefaultInstruments {{
        { 0u, InstrumentKind::MembraneKick, "MEMBRANE KICK", "KCK", true },
        { 1u, InstrumentKind::MembraneKick, "MEMBRANE KICK", "KCK", true },
        { 2u, InstrumentKind::MembraneKick, "MEMBRANE KICK", "KCK", true },
        { 3u, InstrumentKind::MembraneKick, "MEMBRANE KICK", "KCK", true },
        { 4u, InstrumentKind::MembraneKick, "MEMBRANE KICK", "KCK", true },
        { kSn76489InstrumentNode, InstrumentKind::Sn76489Psg,
            "SN76489 PSG", "PSG", true },
        { kSn76489InstrumentNode + 1u, InstrumentKind::Sn76489Psg,
            "SN76489 PSG", "PSG", true },
        { kSn76489InstrumentNode + 2u, InstrumentKind::Sn76489Psg,
            "SN76489 PSG", "PSG", true },
        { kSn76489InstrumentNode + 3u, InstrumentKind::Sn76489Psg,
            "SN76489 PSG", "PSG", true },
        { kSn76489InstrumentNode + 4u, InstrumentKind::Sn76489Psg,
            "SN76489 PSG", "PSG", true },
        { kYm2151InstrumentNode, InstrumentKind::Ym2151Opm,
            "YM2151 OPM", "OPM", true },
        { kYm2151InstrumentNode + 1u, InstrumentKind::Ym2151Opm,
            "YM2151 OPM", "OPM", true },
        { kYm2151InstrumentNode + 2u, InstrumentKind::Ym2151Opm,
            "YM2151 OPM", "OPM", true },
        { kDaisyAnalogBassDrumInstrumentNode,
            InstrumentKind::DaisyAnalogBassDrum,
            "ANALOG BASS DRUM", "ABD", true },
        { kDaisyAnalogBassDrumInstrumentNode + 1u,
            InstrumentKind::DaisyAnalogBassDrum,
            "ANALOG BASS DRUM", "ABD", true },
        { kDaisyAnalogBassDrumInstrumentNode + 2u,
            InstrumentKind::DaisyAnalogBassDrum,
            "ANALOG BASS DRUM", "ABD", true },
        { kDaisyAnalogSnareDrumInstrumentNode,
            InstrumentKind::DaisyAnalogSnareDrum,
            "ANALOG SNARE DRUM", "ASN", true },
        { kDaisyAnalogSnareDrumInstrumentNode + 1u,
            InstrumentKind::DaisyAnalogSnareDrum,
            "ANALOG SNARE DRUM", "ASN", true },
        { kDaisyAnalogSnareDrumInstrumentNode + 2u,
            InstrumentKind::DaisyAnalogSnareDrum,
            "ANALOG SNARE DRUM", "ASN", true },
        { kDaisyHiHatInstrumentNode, InstrumentKind::DaisyHiHat,
            "HI-HAT", "HAT", true },
        { kDaisyHiHatInstrumentNode + 1u, InstrumentKind::DaisyHiHat,
            "HI-HAT", "HAT", true },
        { kDaisyHiHatInstrumentNode + 2u, InstrumentKind::DaisyHiHat,
            "HI-HAT", "HAT", true },
        { kDaisySyntheticBassDrumInstrumentNode,
            InstrumentKind::DaisySyntheticBassDrum,
            "SYNTH BASS DRUM", "SBD", true },
        { kDaisySyntheticBassDrumInstrumentNode + 1u,
            InstrumentKind::DaisySyntheticBassDrum,
            "SYNTH BASS DRUM", "SBD", true },
        { kDaisySyntheticBassDrumInstrumentNode + 2u,
            InstrumentKind::DaisySyntheticBassDrum,
            "SYNTH BASS DRUM", "SBD", true },
        { kDaisySyntheticSnareDrumInstrumentNode,
            InstrumentKind::DaisySyntheticSnareDrum,
            "SYNTH SNARE DRUM", "SSN", true },
        { kDaisySyntheticSnareDrumInstrumentNode + 1u,
            InstrumentKind::DaisySyntheticSnareDrum,
            "SYNTH SNARE DRUM", "SSN", true },
        { kDaisySyntheticSnareDrumInstrumentNode + 2u,
            InstrumentKind::DaisySyntheticSnareDrum,
            "SYNTH SNARE DRUM", "SSN", true },
        { kStereoSamplerInstrumentNode, InstrumentKind::StereoSliceSampler,
            "STEREO SLICE SAMPLER", "SMP", true },
        { kStereoSamplerInstrumentNode + 1u,
            InstrumentKind::StereoSliceSampler,
            "STEREO SLICE SAMPLER", "SMP", true },
        { kStereoSamplerInstrumentNode + 2u,
            InstrumentKind::StereoSliceSampler,
            "STEREO SLICE SAMPLER", "SMP", true },
        { kMidiOutInstrumentNode, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
        { kMidiOutInstrumentNode + 1u, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
        { kMidiOutInstrumentNode + 2u, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
        { kMidiOutInstrumentNode + 3u, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
        { kMidiOutInstrumentNode + 4u, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
        { kMidiOutInstrumentNode + 5u, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
        { kMidiOutInstrumentNode + 6u, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
        { kMidiOutInstrumentNode + 7u, InstrumentKind::MidiOut,
            "MIDI OUT", "MID", true },
    }};

constexpr std::array<InstrumentTypeDefinition,
    kInstrumentTypeCount> kInstrumentTypes {{
    { InstrumentKind::MembraneKick, "MEMBRANE KICK", "KCK",
        kMembraneRackSlotCount },
    { InstrumentKind::DaisyAnalogBassDrum, "ANALOG BASS DRUM", "ABD",
        kDaisyDrumRackSlotCount },
    { InstrumentKind::DaisyAnalogSnareDrum, "ANALOG SNARE DRUM", "ASN",
        kDaisyDrumRackSlotCount },
    { InstrumentKind::DaisyHiHat, "HI-HAT", "HAT",
        kDaisyDrumRackSlotCount },
    { InstrumentKind::DaisySyntheticBassDrum, "SYNTH BASS DRUM", "SBD",
        kDaisyDrumRackSlotCount },
    { InstrumentKind::DaisySyntheticSnareDrum, "SYNTH SNARE DRUM", "SSN",
        kDaisyDrumRackSlotCount },
    { InstrumentKind::StereoSliceSampler, "STEREO SLICE SAMPLER", "SMP",
        kStereoSamplerRackSlotCount },
    { InstrumentKind::MidiOut, "MIDI OUT", "MID",
        kMidiOutRackSlotCount },
}};

constexpr std::array<Sn76489ParameterDefinition,
    kSn76489ParameterCount> kSn76489Parameters {{
        { 0u, "sn76489.tone_mix", "Tone Mix", "",
            Sn76489ParameterScale::Linear, 0.0, 0.35, 0.18 },
        { 1u, "sn76489.noise_mix", "Noise Mix", "",
            Sn76489ParameterScale::Linear, 0.0, 0.50, 0.20 },
        { 2u, "sn76489.noise_clock", "Noise Clock", "Hz",
            Sn76489ParameterScale::Exponential, 100.0, 16000.0, 7800.0 },
        { 3u, "sn76489.noise_decay", "Noise Decay", "ms",
            Sn76489ParameterScale::Exponential, 20.0, 1200.0, 120.0 },
        { 4u, "sn76489.release", "Tone Release", "ms",
            Sn76489ParameterScale::Exponential, 5.0, 500.0, 18.0 },
    }};

constexpr std::array<Ym2151ParameterDefinition,
    kYm2151ParameterCount> kYm2151Parameters {{
        { 0u, "ym2151.program", "Program", Ym2151ControlKind::Stepped,
            0.0, 5.0, 0.0, 6u },
        { 1u, "ym2151.algorithm", "Algorithm", Ym2151ControlKind::Stepped,
            0.0, 7.0, 4.0, 8u },
        { 2u, "ym2151.feedback", "Feedback", Ym2151ControlKind::Stepped,
            0.0, 7.0, 4.0, 8u },
        { 3u, "ym2151.brightness", "Brightness",
            Ym2151ControlKind::Continuous, 0.0, 1.0, 0.64, 0u },
        { 4u, "ym2151.attack", "Attack", Ym2151ControlKind::Continuous,
            0.0, 1.0, 0.88, 0u },
        { 5u, "ym2151.decay", "Decay", Ym2151ControlKind::Continuous,
            0.0, 1.0, 0.48, 0u },
        { 6u, "ym2151.sustain", "Sustain", Ym2151ControlKind::Continuous,
            0.0, 1.0, 0.58, 0u },
        { 7u, "ym2151.release", "Release", Ym2151ControlKind::Continuous,
            0.0, 1.0, 0.42, 0u },
        { 8u, "ym2151.output", "Output", Ym2151ControlKind::Continuous,
            0.0, 1.0, 0.55, 0u },
    }};

constexpr std::array<DaisyDrumParameterDefinition, 7u>
    kDaisyAnalogBassDrumParameters {{
        { 0u, "daisy.abd.frequency", "Frequency", "Hz",
            DaisyDrumParameterScale::Exponential, 25.0, 160.0, 50.0 },
        { 1u, "daisy.abd.accent", "Accent", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.10 },
        { 2u, "daisy.abd.tone", "Tone", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.10 },
        { 3u, "daisy.abd.decay", "Decay", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.30 },
        { 4u, "daisy.abd.attack_fm", "Attack FM", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.50 },
        { 5u, "daisy.abd.self_fm", "Self FM", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 1.00 },
        { 7u, "daisy.abd.output", "Output", "dB",
            DaisyDrumParameterScale::Linear, -36.0, 6.0, -8.0 },
    }};

constexpr std::array<DaisyDrumParameterDefinition, 6u>
    kDaisyAnalogSnareDrumParameters {{
        { 0u, "daisy.asn.frequency", "Frequency", "Hz",
            DaisyDrumParameterScale::Exponential, 80.0, 480.0, 200.0 },
        { 1u, "daisy.asn.accent", "Accent", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.60 },
        { 2u, "daisy.asn.tone", "Tone", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.50 },
        { 3u, "daisy.asn.decay", "Decay", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.30 },
        { 4u, "daisy.asn.snappy", "Snappy", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.70 },
        { 7u, "daisy.asn.output", "Output", "dB",
            DaisyDrumParameterScale::Linear, -36.0, 6.0, -10.0 },
    }};

constexpr std::array<DaisyDrumParameterDefinition, 6u>
    kDaisyHiHatParameters {{
        { 0u, "daisy.hat.frequency", "Frequency", "Hz",
            DaisyDrumParameterScale::Exponential, 300.0, 12000.0, 3000.0 },
        { 1u, "daisy.hat.accent", "Accent", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.80 },
        { 2u, "daisy.hat.tone", "Tone", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.50 },
        { 3u, "daisy.hat.decay", "Decay", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.20 },
        { 4u, "daisy.hat.noisiness", "Noisiness", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.80 },
        { 7u, "daisy.hat.output", "Output", "dB",
            DaisyDrumParameterScale::Linear, -36.0, 6.0, -14.0 },
    }};

constexpr std::array<DaisyDrumParameterDefinition, 8u>
    kDaisySyntheticBassDrumParameters {{
        { 0u, "daisy.sbd.frequency", "Frequency", "Hz",
            DaisyDrumParameterScale::Exponential, 25.0, 180.0, 100.0 },
        { 1u, "daisy.sbd.accent", "Accent", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.20 },
        { 2u, "daisy.sbd.tone", "Tone", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.60 },
        { 3u, "daisy.sbd.decay", "Decay", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.70 },
        { 4u, "daisy.sbd.dirtiness", "Dirtiness", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.30 },
        { 5u, "daisy.sbd.fm_amount", "FM Amount", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.60 },
        { 6u, "daisy.sbd.fm_decay", "FM Decay", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.30 },
        { 7u, "daisy.sbd.output", "Output", "dB",
            DaisyDrumParameterScale::Linear, -36.0, 6.0, -9.0 },
    }};

constexpr std::array<DaisyDrumParameterDefinition, 6u>
    kDaisySyntheticSnareDrumParameters {{
        { 0u, "daisy.ssn.frequency", "Frequency", "Hz",
            DaisyDrumParameterScale::Exponential, 80.0, 600.0, 200.0 },
        { 1u, "daisy.ssn.accent", "Accent", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.60 },
        { 3u, "daisy.ssn.decay", "Decay", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.30 },
        { 4u, "daisy.ssn.fm_amount", "FM Amount", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.10 },
        { 5u, "daisy.ssn.snappy", "Snappy", "",
            DaisyDrumParameterScale::Linear, 0.0, 1.0, 0.70 },
        { 7u, "daisy.ssn.output", "Output", "dB",
            DaisyDrumParameterScale::Linear, -36.0, 6.0, -11.0 },
    }};

constexpr std::array<NamedInstrumentPreset,
    kMembranePresetCount> kMembranePresets {{
        { "DEEP", "Long, low fundamental with a pronounced pitch fall." },
        { "TIGHT", "Short controlled kick for dense polymetric patterns." },
        { "SUB", "Rounded sub-heavy body with reduced click." },
        { "PUNCH", "Fast impact, more click, and a compact tail." },
        { "HOLLOW", "Deformed membrane with a resonant open body." },
    }};

constexpr std::array<NamedInstrumentPreset,
    kYm2151PresetCount> kYm2151Presets {{
        { "METAL BASS", "Dense four-operator bass with audible feedback." },
        { "GLASS BELL", "Bright inharmonic bell with a long release." },
        { "E-PIANO", "Rounded electric-piano transient and decay." },
        { "HOLLOW LEAD", "Nasal sustained lead with moderate feedback." },
        { "PERCUSSIVE", "Short aggressive FM strike." },
        { "SOFT PAD", "Slow, low-feedback layered FM voice." },
    }};

constexpr std::array<NamedInstrumentPreset,
    kDaisyDrumPresetCount> kDaisyAnalogBassDrumPresets {{
        { "CLASSIC", "Rounded analog bass drum with a compact pitch sweep." },
        { "TIGHT", "Short low-end punch for dense breakbeats." },
        { "HARD", "Brighter attack and stronger self modulation." },
        { "SUB", "Low, dark body with a long controlled decay." },
    }};
constexpr std::array<NamedInstrumentPreset,
    kDaisyDrumPresetCount> kDaisyAnalogSnareDrumPresets {{
        { "CLASSIC", "Balanced analog shell and noise response." },
        { "TIGHT", "Short, dry snare for fast patterns." },
        { "NOISE", "Bright snappy noise-dominant strike." },
        { "BODY", "Lower shell tone with a longer decay." },
    }};
constexpr std::array<NamedInstrumentPreset,
    kDaisyDrumPresetCount> kDaisyHiHatPresets {{
        { "CLOSED", "Compact closed metallic hat." },
        { "OPEN", "Longer open-hat envelope with a darker edge." },
        { "BRIGHT", "High, crisp metallic cluster." },
        { "NOISE", "Noisier industrial texture for glitch percussion." },
    }};
constexpr std::array<NamedInstrumentPreset,
    kDaisyDrumPresetCount> kDaisySyntheticBassDrumPresets {{
        { "MODERN", "Balanced synthesized bass drum." },
        { "GABBER", "Hard dirty transient and aggressive pitch envelope." },
        { "SUB", "Clean low body with reduced transient." },
        { "CLICK", "Short bright strike for layered kick attacks." },
    }};
constexpr std::array<NamedInstrumentPreset,
    kDaisyDrumPresetCount> kDaisySyntheticSnareDrumPresets {{
        { "MODERN", "Balanced synthesized snare body and wire noise." },
        { "TIGHT", "Fast compact strike for jungle programming." },
        { "RING", "Pitched resonant body with deeper modulation." },
        { "WIRE", "Noise-forward snare with reduced drum body." },
    }};

constexpr std::array<MembraneParameterDefinition,
    kMembraneParameterCount> kParameters {{
    { 2u, "membrane.shape", "Shape", "", MembraneParameterGroup::Body,
        MembraneControlKind::Stepped, 0.0, 4.0, 0.0, 5u },
    { 3u, "membrane.tune", "Fundamental", "Hz",
        MembraneParameterGroup::Body, MembraneControlKind::Continuous,
        25.0, 90.0, 43.0, 0u },
    { 6u, "membrane.decay", "Decay", "s", MembraneParameterGroup::Body,
        MembraneControlKind::Continuous, 0.08, 6.0, 1.45, 0u },
    { 7u, "membrane.damping", "Damping", "",
        MembraneParameterGroup::Body, MembraneControlKind::Continuous,
        0.0, 1.0, 0.26, 0u },
    { 10u, "membrane.drive", "Drive", "",
        MembraneParameterGroup::Body, MembraneControlKind::Continuous,
        0.0, 1.0, 0.28, 0u },

    { 4u, "membrane.pitch_drop", "Pitch Drop", "st",
        MembraneParameterGroup::Impact, MembraneControlKind::Continuous,
        0.0, 48.0, 31.0, 0u },
    { 5u, "membrane.pitch_time", "Drop Time", "ms",
        MembraneParameterGroup::Impact, MembraneControlKind::Continuous,
        5.0, 250.0, 42.0, 0u },
    { 8u, "membrane.punch", "Punch", "",
        MembraneParameterGroup::Impact, MembraneControlKind::Continuous,
        0.0, 1.0, 0.76, 0u },
    { 9u, "membrane.click", "Click / Noise", "",
        MembraneParameterGroup::Impact, MembraneControlKind::Continuous,
        0.0, 1.0, 0.16, 0u },

    { 21u, "membrane.strike_placement", "Placement", "",
        MembraneParameterGroup::Strike, MembraneControlKind::Stepped,
        0.0, 2.0, 0.0, 3u },
    { 11u, "membrane.strike_x", "Strike X", "",
        MembraneParameterGroup::Strike, MembraneControlKind::Continuous,
        -1.0, 1.0, 0.18, 0u },
    { 12u, "membrane.strike_y", "Strike Y", "",
        MembraneParameterGroup::Strike, MembraneControlKind::Continuous,
        -1.0, 1.0, -0.08, 0u },

    { 13u, "membrane.spread", "Spread", "",
        MembraneParameterGroup::Space, MembraneControlKind::Continuous,
        0.0, 1.0, 0.72, 0u },
    { 14u, "membrane.depth", "Depth", "",
        MembraneParameterGroup::Space, MembraneControlKind::Continuous,
        0.0, 1.0, 0.42, 0u },
    { 15u, "membrane.rotation", "Rotation", "deg",
        MembraneParameterGroup::Space, MembraneControlKind::Continuous,
        -180.0, 180.0, 0.0, 0u },
    { 16u, "membrane.shape_amount", "Shape Amount", "",
        MembraneParameterGroup::Space, MembraneControlKind::Continuous,
        0.0, 1.0, 0.72, 0u },

    { 17u, "membrane.velocity", "Velocity", "",
        MembraneParameterGroup::Response, MembraneControlKind::Continuous,
        0.0, 1.0, 1.0, 0u },
    { 18u, "membrane.note_tracking", "Note Tracking", "",
        MembraneParameterGroup::Response, MembraneControlKind::Continuous,
        0.0, 1.0, 1.0, 0u },
    { 19u, "membrane.output", "Output", "dB",
        MembraneParameterGroup::Response, MembraneControlKind::Continuous,
        -60.0, 6.0, -8.0, 0u },
}};

void setNative(MembranePatch& patch, uint32_t parameterId,
    double value) noexcept
{
    const auto index = membraneParameterIndex(parameterId);
    if (index < patch.normalized.size())
        patch.normalized[index] = membraneNormalizedFromNative(
            parameterId, value);
}

MembranePatch defaultPatch() noexcept
{
    MembranePatch patch;
    for (std::size_t index = 0u; index < kParameters.size(); ++index) {
        patch.normalized[index] = membraneNormalizedFromNative(
            kParameters[index].parameterId,
            kParameters[index].defaultValue);
    }
    return patch;
}

Sn76489Patch defaultSn76489Patch() noexcept
{
    Sn76489Patch patch;
    for (std::size_t index = 0u; index < kSn76489Parameters.size(); ++index) {
        patch.normalized[index] = sn76489NormalizedFromNative(
            kSn76489Parameters[index].parameterId,
            kSn76489Parameters[index].defaultValue);
    }
    return patch;
}

void setYmNative(Ym2151Patch& patch, uint32_t parameterId,
    double value) noexcept
{
    const auto index = ym2151ParameterIndex(parameterId);
    if (index < patch.normalized.size())
        patch.normalized[index] = ym2151NormalizedFromNative(
            parameterId, value);
}

Ym2151Patch ym2151PresetPatch(std::size_t presetIndex) noexcept
{
    Ym2151Patch patch;
    for (std::size_t index = 0u; index < kYm2151Parameters.size(); ++index) {
        patch.normalized[index] = ym2151NormalizedFromNative(
            kYm2151Parameters[index].parameterId,
            kYm2151Parameters[index].defaultValue);
    }
    setYmNative(patch, 0u, static_cast<double>(std::min(
        presetIndex, kYm2151PresetCount - 1u)));
    switch (std::min(presetIndex, kYm2151PresetCount - 1u)) {
    case 0u: // metal bass
        setYmNative(patch, 1u, 4.0); setYmNative(patch, 2u, 5.0);
        setYmNative(patch, 3u, 0.72); setYmNative(patch, 4u, 0.94);
        setYmNative(patch, 5u, 0.52); setYmNative(patch, 6u, 0.54);
        setYmNative(patch, 7u, 0.34); setYmNative(patch, 8u, 0.54);
        break;
    case 1u: // glass bell
        setYmNative(patch, 1u, 3.0); setYmNative(patch, 2u, 2.0);
        setYmNative(patch, 3u, 0.92); setYmNative(patch, 4u, 1.0);
        setYmNative(patch, 5u, 0.32); setYmNative(patch, 6u, 0.22);
        setYmNative(patch, 7u, 0.72); setYmNative(patch, 8u, 0.48);
        break;
    case 2u: // electric piano
        setYmNative(patch, 1u, 5.0); setYmNative(patch, 2u, 3.0);
        setYmNative(patch, 3u, 0.66); setYmNative(patch, 4u, 0.90);
        setYmNative(patch, 5u, 0.44); setYmNative(patch, 6u, 0.38);
        setYmNative(patch, 7u, 0.48); setYmNative(patch, 8u, 0.52);
        break;
    case 3u: // hollow lead
        setYmNative(patch, 1u, 6.0); setYmNative(patch, 2u, 4.0);
        setYmNative(patch, 3u, 0.58); setYmNative(patch, 4u, 0.76);
        setYmNative(patch, 5u, 0.34); setYmNative(patch, 6u, 0.78);
        setYmNative(patch, 7u, 0.56); setYmNative(patch, 8u, 0.50);
        break;
    case 4u: // percussive
        setYmNative(patch, 1u, 7.0); setYmNative(patch, 2u, 6.0);
        setYmNative(patch, 3u, 0.82); setYmNative(patch, 4u, 1.0);
        setYmNative(patch, 5u, 0.78); setYmNative(patch, 6u, 0.10);
        setYmNative(patch, 7u, 0.20); setYmNative(patch, 8u, 0.46);
        break;
    case 5u: // soft pad
        setYmNative(patch, 1u, 7.0); setYmNative(patch, 2u, 1.0);
        setYmNative(patch, 3u, 0.32); setYmNative(patch, 4u, 0.24);
        setYmNative(patch, 5u, 0.24); setYmNative(patch, 6u, 0.82);
        setYmNative(patch, 7u, 0.70); setYmNative(patch, 8u, 0.44);
        break;
    }
    return patch;
}

void setDaisyNative(DaisyDrumPatch& patch, InstrumentKind kind,
    uint32_t parameterId, double value) noexcept
{
    const auto index = daisyDrumParameterIndex(kind, parameterId);
    if (index < patch.normalized.size()) {
        patch.normalized[index] = daisyDrumNormalizedFromNative(
            kind, parameterId, value);
    }
}

DaisyDrumPatch defaultDaisyDrumPatch(InstrumentKind kind) noexcept
{
    DaisyDrumPatch patch;
    const auto count = daisyDrumParameterCount(kind);
    for (std::size_t index = 0u; index < count; ++index) {
        const auto* parameter = daisyDrumParameter(kind, index);
        if (parameter) {
            patch.normalized[index] = daisyDrumNormalizedFromNative(kind,
                parameter->parameterId, parameter->defaultValue);
        }
    }
    return patch;
}

DaisyDrumPatch daisyDrumPresetPatch(InstrumentKind kind,
    std::size_t presetIndex) noexcept
{
    auto patch = defaultDaisyDrumPatch(kind);
    const auto preset = std::min(presetIndex, kDaisyDrumPresetCount - 1u);
    switch (kind) {
    case InstrumentKind::DaisyAnalogBassDrum:
        if (preset == 1u) {
            setDaisyNative(patch, kind, 0u, 62.0);
            setDaisyNative(patch, kind, 2u, 0.38);
            setDaisyNative(patch, kind, 3u, 0.12);
            setDaisyNative(patch, kind, 4u, 0.66);
            setDaisyNative(patch, kind, 5u, 0.55);
        } else if (preset == 2u) {
            setDaisyNative(patch, kind, 0u, 54.0);
            setDaisyNative(patch, kind, 1u, 0.88);
            setDaisyNative(patch, kind, 2u, 0.72);
            setDaisyNative(patch, kind, 4u, 0.94);
            setDaisyNative(patch, kind, 5u, 0.90);
        } else if (preset == 3u) {
            setDaisyNative(patch, kind, 0u, 34.0);
            setDaisyNative(patch, kind, 2u, 0.04);
            setDaisyNative(patch, kind, 3u, 0.78);
            setDaisyNative(patch, kind, 4u, 0.22);
            setDaisyNative(patch, kind, 5u, 0.36);
        }
        break;
    case InstrumentKind::DaisyAnalogSnareDrum:
        if (preset == 1u) {
            setDaisyNative(patch, kind, 0u, 245.0);
            setDaisyNative(patch, kind, 2u, 0.46);
            setDaisyNative(patch, kind, 3u, 0.10);
            setDaisyNative(patch, kind, 4u, 0.60);
        } else if (preset == 2u) {
            setDaisyNative(patch, kind, 0u, 280.0);
            setDaisyNative(patch, kind, 1u, 0.90);
            setDaisyNative(patch, kind, 2u, 0.82);
            setDaisyNative(patch, kind, 4u, 0.96);
        } else if (preset == 3u) {
            setDaisyNative(patch, kind, 0u, 135.0);
            setDaisyNative(patch, kind, 2u, 0.22);
            setDaisyNative(patch, kind, 3u, 0.70);
            setDaisyNative(patch, kind, 4u, 0.42);
        }
        break;
    case InstrumentKind::DaisyHiHat:
        if (preset == 1u) {
            setDaisyNative(patch, kind, 0u, 2200.0);
            setDaisyNative(patch, kind, 2u, 0.38);
            setDaisyNative(patch, kind, 3u, 0.78);
            setDaisyNative(patch, kind, 4u, 0.68);
        } else if (preset == 2u) {
            setDaisyNative(patch, kind, 0u, 7200.0);
            setDaisyNative(patch, kind, 2u, 0.88);
            setDaisyNative(patch, kind, 3u, 0.16);
            setDaisyNative(patch, kind, 4u, 0.58);
        } else if (preset == 3u) {
            setDaisyNative(patch, kind, 0u, 4200.0);
            setDaisyNative(patch, kind, 2u, 0.70);
            setDaisyNative(patch, kind, 3u, 0.44);
            setDaisyNative(patch, kind, 4u, 1.00);
        }
        break;
    case InstrumentKind::DaisySyntheticBassDrum:
        if (preset == 1u) {
            setDaisyNative(patch, kind, 0u, 54.0);
            setDaisyNative(patch, kind, 1u, 0.92);
            setDaisyNative(patch, kind, 2u, 0.86);
            setDaisyNative(patch, kind, 3u, 0.58);
            setDaisyNative(patch, kind, 4u, 0.96);
            setDaisyNative(patch, kind, 5u, 0.92);
            setDaisyNative(patch, kind, 6u, 0.20);
        } else if (preset == 2u) {
            setDaisyNative(patch, kind, 0u, 38.0);
            setDaisyNative(patch, kind, 2u, 0.18);
            setDaisyNative(patch, kind, 3u, 0.88);
            setDaisyNative(patch, kind, 4u, 0.08);
            setDaisyNative(patch, kind, 5u, 0.20);
        } else if (preset == 3u) {
            setDaisyNative(patch, kind, 0u, 92.0);
            setDaisyNative(patch, kind, 2u, 1.00);
            setDaisyNative(patch, kind, 3u, 0.12);
            setDaisyNative(patch, kind, 4u, 0.46);
            setDaisyNative(patch, kind, 5u, 0.78);
            setDaisyNative(patch, kind, 6u, 0.08);
        }
        break;
    case InstrumentKind::DaisySyntheticSnareDrum:
        if (preset == 1u) {
            setDaisyNative(patch, kind, 0u, 260.0);
            setDaisyNative(patch, kind, 3u, 0.10);
            setDaisyNative(patch, kind, 4u, 0.08);
            setDaisyNative(patch, kind, 5u, 0.64);
        } else if (preset == 2u) {
            setDaisyNative(patch, kind, 0u, 155.0);
            setDaisyNative(patch, kind, 3u, 0.72);
            setDaisyNative(patch, kind, 4u, 0.82);
            setDaisyNative(patch, kind, 5u, 0.32);
        } else if (preset == 3u) {
            setDaisyNative(patch, kind, 0u, 220.0);
            setDaisyNative(patch, kind, 3u, 0.48);
            setDaisyNative(patch, kind, 4u, 0.18);
            setDaisyNative(patch, kind, 5u, 1.00);
        }
        break;
    default:
        break;
    }
    return patch;
}

MembranePatch membranePresetPatch(std::size_t presetIndex) noexcept
{
    auto patch = defaultPatch();
    switch (std::min(presetIndex, kMembranePresetCount - 1u)) {
    case 0u: // deep
        break;
    case 1u: // tight
        setNative(patch, 3u, 56.0); setNative(patch, 4u, 20.0);
        setNative(patch, 5u, 24.0); setNative(patch, 6u, 0.42);
        setNative(patch, 7u, 0.52); setNative(patch, 8u, 0.82);
        setNative(patch, 9u, 0.22); setNative(patch, 10u, 0.25);
        break;
    case 2u: // sub
        setNative(patch, 3u, 34.0); setNative(patch, 4u, 18.0);
        setNative(patch, 5u, 58.0); setNative(patch, 6u, 2.4);
        setNative(patch, 7u, 0.18); setNative(patch, 8u, 0.62);
        setNative(patch, 9u, 0.04); setNative(patch, 10u, 0.18);
        break;
    case 3u: // punch
        setNative(patch, 3u, 48.0); setNative(patch, 4u, 38.0);
        setNative(patch, 5u, 22.0); setNative(patch, 6u, 0.68);
        setNative(patch, 7u, 0.36); setNative(patch, 8u, 0.94);
        setNative(patch, 9u, 0.38); setNative(patch, 10u, 0.48);
        break;
    case 4u: // hollow
        setNative(patch, 2u, 1.0); setNative(patch, 3u, 45.0);
        setNative(patch, 4u, 24.0); setNative(patch, 5u, 46.0);
        setNative(patch, 6u, 1.9); setNative(patch, 7u, 0.28);
        setNative(patch, 8u, 0.70); setNative(patch, 9u, 0.10);
        setNative(patch, 16u, 0.78); setNative(patch, 10u, 0.32);
        break;
    }
    return patch;
}

MembranePatch rolePatch(MembraneInstrumentRole role) noexcept
{
    auto patch = defaultPatch();
    switch (role) {
    case MembraneInstrumentRole::Kick:
        break;
    case MembraneInstrumentRole::SnareBody:
        setNative(patch, 2u, 4.0);
        setNative(patch, 3u, 90.0);
        setNative(patch, 4u, 3.0);
        setNative(patch, 5u, 12.0);
        setNative(patch, 6u, 0.42);
        setNative(patch, 7u, 0.48);
        setNative(patch, 8u, 0.88);
        setNative(patch, 9u, 0.78);
        setNative(patch, 10u, 0.34);
        setNative(patch, 11u, 0.37);
        setNative(patch, 12u, -0.21);
        setNative(patch, 13u, 0.78);
        setNative(patch, 14u, 0.28);
        setNative(patch, 15u, 18.0);
        setNative(patch, 16u, 0.84);
        setNative(patch, 19u, -11.0);
        break;
    case MembraneInstrumentRole::FloorTom:
        setNative(patch, 2u, 1.0);
        setNative(patch, 3u, 42.0);
        setNative(patch, 4u, 8.0);
        setNative(patch, 5u, 32.0);
        setNative(patch, 6u, 1.85);
        setNative(patch, 7u, 0.42);
        setNative(patch, 8u, 0.80);
        setNative(patch, 9u, 0.08);
        setNative(patch, 10u, 0.27);
        setNative(patch, 11u, -0.33);
        setNative(patch, 12u, 0.18);
        setNative(patch, 13u, 0.72);
        setNative(patch, 14u, 0.66);
        setNative(patch, 15u, -38.0);
        setNative(patch, 16u, 0.52);
        setNative(patch, 18u, 0.35);
        setNative(patch, 19u, -11.0);
        break;
    case MembraneInstrumentRole::LowTom:
        setNative(patch, 2u, 0.0);
        setNative(patch, 3u, 55.0);
        setNative(patch, 4u, 6.0);
        setNative(patch, 5u, 24.0);
        setNative(patch, 6u, 1.15);
        setNative(patch, 7u, 0.38);
        setNative(patch, 8u, 0.82);
        setNative(patch, 9u, 0.10);
        setNative(patch, 10u, 0.24);
        setNative(patch, 11u, 0.24);
        setNative(patch, 12u, 0.22);
        setNative(patch, 13u, 0.64);
        setNative(patch, 14u, 0.52);
        setNative(patch, 15u, 28.0);
        setNative(patch, 16u, 0.20);
        setNative(patch, 18u, 0.25);
        setNative(patch, 19u, -11.0);
        break;
    case MembraneInstrumentRole::HighTom:
        setNative(patch, 2u, 2.0);
        setNative(patch, 3u, 72.0);
        setNative(patch, 4u, 4.0);
        setNative(patch, 5u, 18.0);
        setNative(patch, 6u, 0.72);
        setNative(patch, 7u, 0.33);
        setNative(patch, 8u, 0.86);
        setNative(patch, 9u, 0.14);
        setNative(patch, 10u, 0.22);
        setNative(patch, 11u, 0.38);
        setNative(patch, 12u, -0.27);
        setNative(patch, 13u, 0.58);
        setNative(patch, 14u, 0.38);
        setNative(patch, 15u, 62.0);
        setNative(patch, 16u, 0.72);
        setNative(patch, 18u, 0.20);
        setNative(patch, 19u, -11.0);
        break;
    }
    return patch;
}

} // namespace

const RackInstrument* rackInstrument(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept
{
    for (const auto& instrument : rack.instruments) {
        if (instrument.active && instrument.nodeId == nodeId)
            return &instrument;
    }
    return nullptr;
}

const RackInstrument* rackInstrumentAt(const InstrumentRackState& rack,
    std::size_t rackIndex) noexcept
{
    if (rackIndex >= rack.instruments.size()
        || !rack.instruments[rackIndex].active) return nullptr;
    return &rack.instruments[rackIndex];
}

std::size_t rackIndexForNode(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept
{
    for (std::size_t index = 0u; index < rack.instruments.size(); ++index) {
        if (rack.instruments[index].active
            && rack.instruments[index].nodeId == nodeId) return index;
    }
    return rack.instruments.size();
}

std::size_t activeInstrumentCount(const InstrumentRackState& rack) noexcept
{
    return static_cast<std::size_t>(std::count_if(rack.instruments.begin(),
        rack.instruments.end(), [](const auto& instrument) {
            return instrument.active;
        }));
}

std::size_t activeInstrumentCount(const InstrumentRackState& rack,
    InstrumentKind kind) noexcept
{
    return static_cast<std::size_t>(std::count_if(rack.instruments.begin(),
        rack.instruments.end(), [kind](const auto& instrument) {
            return instrument.active && instrument.kind == kind;
        }));
}

uint32_t cycleActiveInstrument(const InstrumentRackState& rack,
    uint32_t nodeId, int direction) noexcept
{
    const auto count = activeInstrumentCount(rack);
    if (count == 0u) return kInvalidInstrumentNode;
    auto index = rackIndexForNode(rack, nodeId);
    if (index >= rack.instruments.size())
        index = direction < 0 ? 0u : rack.instruments.size() - 1u;
    const int step = direction < 0 ? -1 : 1;
    for (std::size_t attempt = 0u; attempt < rack.instruments.size();
         ++attempt) {
        const auto next = static_cast<std::size_t>((
            static_cast<int>(index) + step
                + static_cast<int>(rack.instruments.size()))
            % static_cast<int>(rack.instruments.size()));
        index = next;
        if (rack.instruments[index].active)
            return rack.instruments[index].nodeId;
    }
    return kInvalidInstrumentNode;
}

std::size_t instrumentTypeCount() noexcept { return kInstrumentTypes.size(); }

const InstrumentTypeDefinition* instrumentType(std::size_t index) noexcept
{
    return index < kInstrumentTypes.size() ? &kInstrumentTypes[index]
                                          : nullptr;
}

bool canAddInstrumentInstance(const InstrumentRackState& rack,
    InstrumentKind kind) noexcept
{
    const auto found = std::find_if(kInstrumentTypes.begin(),
        kInstrumentTypes.end(), [kind](const auto& type) {
            return type.kind == kind;
        });
    return found != kInstrumentTypes.end()
        && activeInstrumentCount(rack) < rack.instruments.size()
        && activeInstrumentCount(rack, kind) < found->maximumInstances;
}

bool addInstrumentInstance(InstrumentRackState& rack, InstrumentKind kind,
    std::size_t* addedRackIndex, uint32_t* addedNodeId) noexcept
{
    if (!canAddInstrumentInstance(rack, kind)) return false;
    const auto type = std::find_if(kInstrumentTypes.begin(),
        kInstrumentTypes.end(), [kind](const auto& candidate) {
            return candidate.kind == kind;
        });
    if (type == kInstrumentTypes.end()) return false;

    uint32_t nodeId = kInvalidInstrumentNode;
    if (kind == InstrumentKind::MembraneKick) {
        for (uint32_t candidate = 0u;
             candidate < static_cast<uint32_t>(kMembraneRackSlotCount);
             ++candidate) {
            if (!rackInstrument(rack, candidate)) {
                nodeId = candidate;
                break;
            }
        }
    } else if (kind == InstrumentKind::Sn76489Psg) {
        for (std::size_t slot = 0u; slot < kSn76489RackSlotCount; ++slot) {
            const auto candidate = sn76489NodeForRackSlot(slot);
            if (!rackInstrument(rack, candidate)) {
                nodeId = candidate;
                break;
            }
        }
    } else if (kind == InstrumentKind::Ym2151Opm) {
        for (std::size_t slot = 0u; slot < kYm2151RackSlotCount; ++slot) {
            const auto candidate = ym2151NodeForRackSlot(slot);
            if (!rackInstrument(rack, candidate)) {
                nodeId = candidate;
                break;
            }
        }
    } else if (isDaisyDrumKind(kind)) {
        for (std::size_t slot = 0u; slot < kDaisyDrumRackSlotCount; ++slot) {
            const auto candidate = daisyDrumNodeForRackSlot(kind, slot);
            if (!rackInstrument(rack, candidate)) {
                nodeId = candidate;
                break;
            }
        }
    } else if (kind == InstrumentKind::StereoSliceSampler) {
        for (std::size_t slot = 0u; slot < kStereoSamplerRackSlotCount;
             ++slot) {
            const auto candidate = stereoSamplerNodeForRackSlot(slot);
            if (!rackInstrument(rack, candidate)) {
                nodeId = candidate;
                break;
            }
        }
    } else if (kind == InstrumentKind::MidiOut) {
        for (std::size_t slot = 0u; slot < kMidiOutRackSlotCount; ++slot) {
            const auto candidate = midiOutNodeForRackSlot(slot);
            if (!rackInstrument(rack, candidate)) {
                nodeId = candidate;
                break;
            }
        }
    }
    if (nodeId == kInvalidInstrumentNode) return false;

    for (std::size_t index = 0u; index < rack.instruments.size(); ++index) {
        if (rack.instruments[index].active) continue;
        rack.instruments[index] = { nodeId, kind, type->name,
            type->mnemonic, true };
        if (addedRackIndex) *addedRackIndex = index;
        if (addedNodeId) *addedNodeId = nodeId;
        return true;
    }
    return false;
}

bool isSn76489InstrumentNode(uint32_t nodeId) noexcept
{
    return nodeId >= kSn76489InstrumentNode
        && nodeId < kSn76489InstrumentNode
            + static_cast<uint32_t>(kSn76489RackSlotCount);
}

std::size_t sn76489RackSlotIndex(uint32_t nodeId) noexcept
{
    return isSn76489InstrumentNode(nodeId)
        ? static_cast<std::size_t>(nodeId - kSn76489InstrumentNode)
        : kSn76489RackSlotCount;
}

uint32_t sn76489NodeForRackSlot(std::size_t slot) noexcept
{
    return slot < kSn76489RackSlotCount
        ? kSn76489InstrumentNode + static_cast<uint32_t>(slot)
        : kInvalidInstrumentNode;
}

bool isYm2151InstrumentNode(uint32_t nodeId) noexcept
{
    return nodeId >= kYm2151InstrumentNode
        && nodeId < kYm2151InstrumentNode
            + static_cast<uint32_t>(kYm2151RackSlotCount);
}

std::size_t ym2151RackSlotIndex(uint32_t nodeId) noexcept
{
    return isYm2151InstrumentNode(nodeId)
        ? static_cast<std::size_t>(nodeId - kYm2151InstrumentNode)
        : kYm2151RackSlotCount;
}

uint32_t ym2151NodeForRackSlot(std::size_t slot) noexcept
{
    return slot < kYm2151RackSlotCount
        ? kYm2151InstrumentNode + static_cast<uint32_t>(slot)
        : kInvalidInstrumentNode;
}

bool isDaisyDrumKind(InstrumentKind kind) noexcept
{
    return kind >= InstrumentKind::DaisyAnalogBassDrum
        && kind <= InstrumentKind::DaisySyntheticSnareDrum;
}

uint32_t daisyDrumFirstNode(InstrumentKind kind) noexcept
{
    switch (kind) {
    case InstrumentKind::DaisyAnalogBassDrum:
        return kDaisyAnalogBassDrumInstrumentNode;
    case InstrumentKind::DaisyAnalogSnareDrum:
        return kDaisyAnalogSnareDrumInstrumentNode;
    case InstrumentKind::DaisyHiHat:
        return kDaisyHiHatInstrumentNode;
    case InstrumentKind::DaisySyntheticBassDrum:
        return kDaisySyntheticBassDrumInstrumentNode;
    case InstrumentKind::DaisySyntheticSnareDrum:
        return kDaisySyntheticSnareDrumInstrumentNode;
    default:
        return kInvalidInstrumentNode;
    }
}

bool isDaisyDrumInstrumentNode(uint32_t nodeId) noexcept
{
    return nodeId >= kDaisyAnalogBassDrumInstrumentNode
        && nodeId < kDaisyAnalogBassDrumInstrumentNode
            + static_cast<uint32_t>(kDaisyDrumNodeCount);
}

InstrumentKind daisyDrumKindForNode(uint32_t nodeId) noexcept
{
    if (!isDaisyDrumInstrumentNode(nodeId))
        return InstrumentKind::MembraneKick;
    const auto kindOffset = static_cast<uint8_t>((
        nodeId - kDaisyAnalogBassDrumInstrumentNode)
        / static_cast<uint32_t>(kDaisyDrumRackSlotCount));
    return static_cast<InstrumentKind>(
        static_cast<uint8_t>(InstrumentKind::DaisyAnalogBassDrum)
        + kindOffset);
}

std::size_t daisyDrumRackSlotIndex(uint32_t nodeId) noexcept
{
    return isDaisyDrumInstrumentNode(nodeId)
        ? static_cast<std::size_t>((nodeId
            - kDaisyAnalogBassDrumInstrumentNode)
            % static_cast<uint32_t>(kDaisyDrumRackSlotCount))
        : kDaisyDrumRackSlotCount;
}

std::size_t daisyDrumPatchIndex(uint32_t nodeId) noexcept
{
    return isDaisyDrumInstrumentNode(nodeId)
        ? static_cast<std::size_t>(nodeId
            - kDaisyAnalogBassDrumInstrumentNode)
        : kDaisyDrumNodeCount;
}

uint32_t daisyDrumNodeForRackSlot(InstrumentKind kind,
    std::size_t slot) noexcept
{
    const auto first = daisyDrumFirstNode(kind);
    return first != kInvalidInstrumentNode && slot < kDaisyDrumRackSlotCount
        ? first + static_cast<uint32_t>(slot)
        : kInvalidInstrumentNode;
}

bool isStereoSamplerInstrumentNode(uint32_t nodeId) noexcept
{
    return nodeId >= kStereoSamplerInstrumentNode
        && nodeId < kStereoSamplerInstrumentNode
            + static_cast<uint32_t>(kStereoSamplerRackSlotCount);
}

std::size_t stereoSamplerRackSlotIndex(uint32_t nodeId) noexcept
{
    return isStereoSamplerInstrumentNode(nodeId)
        ? static_cast<std::size_t>(nodeId - kStereoSamplerInstrumentNode)
        : kStereoSamplerRackSlotCount;
}

uint32_t stereoSamplerNodeForRackSlot(std::size_t slot) noexcept
{
    return slot < kStereoSamplerRackSlotCount
        ? kStereoSamplerInstrumentNode + static_cast<uint32_t>(slot)
        : kInvalidInstrumentNode;
}

bool isMidiOutInstrumentNode(uint32_t nodeId) noexcept
{
    return nodeId >= kMidiOutInstrumentNode
        && nodeId < kMidiOutInstrumentNode
            + static_cast<uint32_t>(kMidiOutRackSlotCount);
}

std::size_t midiOutRackSlotIndex(uint32_t nodeId) noexcept
{
    return isMidiOutInstrumentNode(nodeId)
        ? static_cast<std::size_t>(nodeId - kMidiOutInstrumentNode)
        : kMidiOutRackSlotCount;
}

uint32_t midiOutNodeForRackSlot(std::size_t slot) noexcept
{
    return slot < kMidiOutRackSlotCount
        ? kMidiOutInstrumentNode + static_cast<uint32_t>(slot)
        : kInvalidInstrumentNode;
}

const RackInstrument* defaultRackInstrument(uint32_t nodeId) noexcept
{
    for (const auto& instrument : kDefaultInstruments) {
        if (instrument.nodeId == nodeId) return &instrument;
    }
    return nullptr;
}

std::string_view instrumentKindName(InstrumentKind kind) noexcept
{
    switch (kind) {
    case InstrumentKind::MembraneKick: return "MEMBRANE KICK";
    case InstrumentKind::Sn76489Psg: return "SN76489 PSG";
    case InstrumentKind::Ym2151Opm: return "YM2151 OPM";
    case InstrumentKind::DaisyAnalogBassDrum: return "ANALOG BASS DRUM";
    case InstrumentKind::DaisyAnalogSnareDrum: return "ANALOG SNARE DRUM";
    case InstrumentKind::DaisyHiHat: return "HI-HAT";
    case InstrumentKind::DaisySyntheticBassDrum: return "SYNTH BASS DRUM";
    case InstrumentKind::DaisySyntheticSnareDrum: return "SYNTH SNARE DRUM";
    case InstrumentKind::StereoSliceSampler: return "STEREO SLICE SAMPLER";
    case InstrumentKind::MidiOut: return "MIDI OUT";
    }
    return "INSTRUMENT";
}

bool instrumentRoutesToInternal(InstrumentKind kind) noexcept
{
    return kind != InstrumentKind::MidiOut
        && kind != InstrumentKind::Sn76489Psg
        && kind != InstrumentKind::Ym2151Opm;
}

bool instrumentRoutesToMidi(InstrumentKind kind) noexcept
{
    return kind == InstrumentKind::MidiOut;
}

std::size_t sn76489ParameterCount() noexcept
{
    return kSn76489Parameters.size();
}

const Sn76489ParameterDefinition* sn76489Parameter(
    std::size_t index) noexcept
{
    return index < kSn76489Parameters.size()
        ? &kSn76489Parameters[index] : nullptr;
}

const Sn76489ParameterDefinition* findSn76489Parameter(
    uint32_t parameterId) noexcept
{
    for (const auto& parameter : kSn76489Parameters) {
        if (parameter.parameterId == parameterId) return &parameter;
    }
    return nullptr;
}

std::size_t sn76489ParameterIndex(uint32_t parameterId) noexcept
{
    for (std::size_t index = 0u; index < kSn76489Parameters.size(); ++index) {
        if (kSn76489Parameters[index].parameterId == parameterId) return index;
    }
    return kSn76489Parameters.size();
}

double sn76489NativeFromNormalized(uint32_t parameterId,
    float normalized) noexcept
{
    const auto* parameter = findSn76489Parameter(parameterId);
    if (!parameter) return 0.0;
    const double unit = std::clamp(std::isfinite(normalized)
            ? static_cast<double>(normalized) : 0.0,
        0.0, 1.0);
    if (parameter->scale == Sn76489ParameterScale::Exponential
        && parameter->minimum > 0.0 && parameter->maximum > parameter->minimum) {
        return parameter->minimum * std::pow(
            parameter->maximum / parameter->minimum, unit);
    }
    return parameter->minimum
        + unit * (parameter->maximum - parameter->minimum);
}

float sn76489NormalizedFromNative(uint32_t parameterId,
    double nativeValue) noexcept
{
    const auto* parameter = findSn76489Parameter(parameterId);
    if (!parameter || !(parameter->maximum > parameter->minimum)) return 0.0f;
    const double value = std::clamp(std::isfinite(nativeValue)
            ? nativeValue : parameter->defaultValue,
        parameter->minimum, parameter->maximum);
    if (parameter->scale == Sn76489ParameterScale::Exponential
        && parameter->minimum > 0.0) {
        return static_cast<float>(std::clamp(
            std::log(value / parameter->minimum)
                / std::log(parameter->maximum / parameter->minimum),
            0.0, 1.0));
    }
    return static_cast<float>((value - parameter->minimum)
        / (parameter->maximum - parameter->minimum));
}

bool setSn76489BaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept
{
    const auto slot = sn76489RackSlotIndex(nodeId);
    const auto index = sn76489ParameterIndex(parameterId);
    if (slot >= rack.sn76489Patches.size()
        || index >= rack.sn76489Patches[slot].normalized.size()) return false;
    rack.sn76489Patches[slot].normalized[index] = std::clamp(
        std::isfinite(normalized) ? normalized : 0.0f, 0.0f, 1.0f);
    return true;
}

float sn76489BaseParameter(const InstrumentRackState& rack,
    uint32_t nodeId, uint32_t parameterId) noexcept
{
    const auto slot = sn76489RackSlotIndex(nodeId);
    const auto index = sn76489ParameterIndex(parameterId);
    return slot < rack.sn76489Patches.size()
            && index < rack.sn76489Patches[slot].normalized.size()
        ? rack.sn76489Patches[slot].normalized[index] : 0.0f;
}

std::size_t ym2151ParameterCount() noexcept
{
    return kYm2151Parameters.size();
}

const Ym2151ParameterDefinition* ym2151Parameter(
    std::size_t index) noexcept
{
    return index < kYm2151Parameters.size()
        ? &kYm2151Parameters[index] : nullptr;
}

std::size_t ym2151ParameterIndex(uint32_t parameterId) noexcept
{
    for (std::size_t index = 0u; index < kYm2151Parameters.size(); ++index) {
        if (kYm2151Parameters[index].parameterId == parameterId) return index;
    }
    return kYm2151Parameters.size();
}

double ym2151NativeFromNormalized(uint32_t parameterId,
    float normalized) noexcept
{
    const auto index = ym2151ParameterIndex(parameterId);
    if (index >= kYm2151Parameters.size()) return 0.0;
    const auto& parameter = kYm2151Parameters[index];
    const double unit = std::clamp(std::isfinite(normalized)
            ? static_cast<double>(normalized) : 0.0,
        0.0, 1.0);
    double value = parameter.minimum
        + unit * (parameter.maximum - parameter.minimum);
    if (parameter.control == Ym2151ControlKind::Stepped)
        value = std::round(value);
    return value;
}

float ym2151NormalizedFromNative(uint32_t parameterId,
    double nativeValue) noexcept
{
    const auto index = ym2151ParameterIndex(parameterId);
    if (index >= kYm2151Parameters.size()) return 0.0f;
    const auto& parameter = kYm2151Parameters[index];
    if (!(parameter.maximum > parameter.minimum)) return 0.0f;
    const double value = std::clamp(std::isfinite(nativeValue)
            ? nativeValue : parameter.defaultValue,
        parameter.minimum, parameter.maximum);
    return static_cast<float>((value - parameter.minimum)
        / (parameter.maximum - parameter.minimum));
}

bool setYm2151BaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept
{
    const auto slot = ym2151RackSlotIndex(nodeId);
    const auto index = ym2151ParameterIndex(parameterId);
    if (slot >= rack.ym2151Patches.size()
        || index >= rack.ym2151Patches[slot].normalized.size()) return false;
    rack.ym2151Patches[slot].normalized[index] = std::clamp(
        std::isfinite(normalized) ? normalized : 0.0f, 0.0f, 1.0f);
    return true;
}

float ym2151BaseParameter(const InstrumentRackState& rack,
    uint32_t nodeId, uint32_t parameterId) noexcept
{
    const auto slot = ym2151RackSlotIndex(nodeId);
    const auto index = ym2151ParameterIndex(parameterId);
    return slot < rack.ym2151Patches.size()
            && index < rack.ym2151Patches[slot].normalized.size()
        ? rack.ym2151Patches[slot].normalized[index] : 0.0f;
}

const NamedInstrumentPreset* ym2151Preset(std::size_t index) noexcept
{
    return index < kYm2151Presets.size() ? &kYm2151Presets[index] : nullptr;
}

std::size_t ym2151PresetIndex(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept
{
    const auto slot = ym2151RackSlotIndex(nodeId);
    if (slot >= rack.ym2151Patches.size()) return kYm2151PresetCount;
    for (std::size_t index = 0u; index < kYm2151PresetCount; ++index) {
        if (rack.ym2151Patches[slot].normalized
            == ym2151PresetPatch(index).normalized) return index;
    }
    return kYm2151PresetCount;
}

bool applyYm2151Preset(InstrumentRackState& rack, uint32_t nodeId,
    std::size_t presetIndex) noexcept
{
    const auto slot = ym2151RackSlotIndex(nodeId);
    if (slot >= rack.ym2151Patches.size()
        || presetIndex >= kYm2151PresetCount) return false;
    rack.ym2151Patches[slot] = ym2151PresetPatch(presetIndex);
    return true;
}

std::size_t daisyDrumParameterCount(InstrumentKind kind) noexcept
{
    switch (kind) {
    case InstrumentKind::DaisyAnalogBassDrum:
        return kDaisyAnalogBassDrumParameters.size();
    case InstrumentKind::DaisyAnalogSnareDrum:
        return kDaisyAnalogSnareDrumParameters.size();
    case InstrumentKind::DaisyHiHat:
        return kDaisyHiHatParameters.size();
    case InstrumentKind::DaisySyntheticBassDrum:
        return kDaisySyntheticBassDrumParameters.size();
    case InstrumentKind::DaisySyntheticSnareDrum:
        return kDaisySyntheticSnareDrumParameters.size();
    default:
        return 0u;
    }
}

const DaisyDrumParameterDefinition* daisyDrumParameter(
    InstrumentKind kind, std::size_t index) noexcept
{
    switch (kind) {
    case InstrumentKind::DaisyAnalogBassDrum:
        return index < kDaisyAnalogBassDrumParameters.size()
            ? &kDaisyAnalogBassDrumParameters[index] : nullptr;
    case InstrumentKind::DaisyAnalogSnareDrum:
        return index < kDaisyAnalogSnareDrumParameters.size()
            ? &kDaisyAnalogSnareDrumParameters[index] : nullptr;
    case InstrumentKind::DaisyHiHat:
        return index < kDaisyHiHatParameters.size()
            ? &kDaisyHiHatParameters[index] : nullptr;
    case InstrumentKind::DaisySyntheticBassDrum:
        return index < kDaisySyntheticBassDrumParameters.size()
            ? &kDaisySyntheticBassDrumParameters[index] : nullptr;
    case InstrumentKind::DaisySyntheticSnareDrum:
        return index < kDaisySyntheticSnareDrumParameters.size()
            ? &kDaisySyntheticSnareDrumParameters[index] : nullptr;
    default:
        return nullptr;
    }
}

const DaisyDrumParameterDefinition* findDaisyDrumParameter(
    InstrumentKind kind, uint32_t parameterId) noexcept
{
    const auto count = daisyDrumParameterCount(kind);
    for (std::size_t index = 0u; index < count; ++index) {
        const auto* parameter = daisyDrumParameter(kind, index);
        if (parameter && parameter->parameterId == parameterId)
            return parameter;
    }
    return nullptr;
}

std::size_t daisyDrumParameterIndex(InstrumentKind kind,
    uint32_t parameterId) noexcept
{
    const auto count = daisyDrumParameterCount(kind);
    for (std::size_t index = 0u; index < count; ++index) {
        const auto* parameter = daisyDrumParameter(kind, index);
        if (parameter && parameter->parameterId == parameterId) return index;
    }
    return kDaisyDrumParameterCapacity;
}

double daisyDrumNativeFromNormalized(InstrumentKind kind,
    uint32_t parameterId, float normalized) noexcept
{
    const auto* parameter = findDaisyDrumParameter(kind, parameterId);
    if (!parameter) return 0.0;
    const double unit = std::clamp(std::isfinite(normalized)
            ? static_cast<double>(normalized) : 0.0,
        0.0, 1.0);
    if (parameter->scale == DaisyDrumParameterScale::Exponential
        && parameter->minimum > 0.0 && parameter->maximum > parameter->minimum) {
        return parameter->minimum * std::pow(
            parameter->maximum / parameter->minimum, unit);
    }
    return parameter->minimum
        + unit * (parameter->maximum - parameter->minimum);
}

float daisyDrumNormalizedFromNative(InstrumentKind kind,
    uint32_t parameterId, double nativeValue) noexcept
{
    const auto* parameter = findDaisyDrumParameter(kind, parameterId);
    if (!parameter || !(parameter->maximum > parameter->minimum)) return 0.0f;
    const double value = std::clamp(std::isfinite(nativeValue)
            ? nativeValue : parameter->defaultValue,
        parameter->minimum, parameter->maximum);
    if (parameter->scale == DaisyDrumParameterScale::Exponential
        && parameter->minimum > 0.0) {
        return static_cast<float>(std::clamp(
            std::log(value / parameter->minimum)
                / std::log(parameter->maximum / parameter->minimum),
            0.0, 1.0));
    }
    return static_cast<float>((value - parameter->minimum)
        / (parameter->maximum - parameter->minimum));
}

bool setDaisyDrumBaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept
{
    const auto patchIndex = daisyDrumPatchIndex(nodeId);
    const auto kind = daisyDrumKindForNode(nodeId);
    const auto parameterIndex = daisyDrumParameterIndex(kind, parameterId);
    if (patchIndex >= rack.daisyDrumPatches.size()
        || parameterIndex >= daisyDrumParameterCount(kind)) return false;
    rack.daisyDrumPatches[patchIndex].normalized[parameterIndex] = std::clamp(
        std::isfinite(normalized) ? normalized : 0.0f, 0.0f, 1.0f);
    return true;
}

float daisyDrumBaseParameter(const InstrumentRackState& rack,
    uint32_t nodeId, uint32_t parameterId) noexcept
{
    const auto patchIndex = daisyDrumPatchIndex(nodeId);
    const auto kind = daisyDrumKindForNode(nodeId);
    const auto parameterIndex = daisyDrumParameterIndex(kind, parameterId);
    return patchIndex < rack.daisyDrumPatches.size()
            && parameterIndex < daisyDrumParameterCount(kind)
        ? rack.daisyDrumPatches[patchIndex].normalized[parameterIndex] : 0.0f;
}

const NamedInstrumentPreset* daisyDrumPreset(InstrumentKind kind,
    std::size_t index) noexcept
{
    if (index >= kDaisyDrumPresetCount) return nullptr;
    switch (kind) {
    case InstrumentKind::DaisyAnalogBassDrum:
        return &kDaisyAnalogBassDrumPresets[index];
    case InstrumentKind::DaisyAnalogSnareDrum:
        return &kDaisyAnalogSnareDrumPresets[index];
    case InstrumentKind::DaisyHiHat:
        return &kDaisyHiHatPresets[index];
    case InstrumentKind::DaisySyntheticBassDrum:
        return &kDaisySyntheticBassDrumPresets[index];
    case InstrumentKind::DaisySyntheticSnareDrum:
        return &kDaisySyntheticSnareDrumPresets[index];
    default:
        return nullptr;
    }
}

std::size_t daisyDrumPresetIndex(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept
{
    const auto patchIndex = daisyDrumPatchIndex(nodeId);
    const auto kind = daisyDrumKindForNode(nodeId);
    if (patchIndex >= rack.daisyDrumPatches.size()
        || !isDaisyDrumKind(kind)) return kDaisyDrumPresetCount;
    for (std::size_t index = 0u; index < kDaisyDrumPresetCount; ++index) {
        if (rack.daisyDrumPatches[patchIndex].normalized
            == daisyDrumPresetPatch(kind, index).normalized) return index;
    }
    return kDaisyDrumPresetCount;
}

bool applyDaisyDrumPreset(InstrumentRackState& rack, uint32_t nodeId,
    std::size_t presetIndex) noexcept
{
    const auto patchIndex = daisyDrumPatchIndex(nodeId);
    const auto kind = daisyDrumKindForNode(nodeId);
    if (patchIndex >= rack.daisyDrumPatches.size()
        || !isDaisyDrumKind(kind)
        || presetIndex >= kDaisyDrumPresetCount) return false;
    rack.daisyDrumPatches[patchIndex] = daisyDrumPresetPatch(kind, presetIndex);
    return true;
}

const MidiInstrumentRoute* midiInstrumentRoute(
    const InstrumentRackState& rack, uint32_t nodeId) noexcept
{
    const auto slot = midiOutRackSlotIndex(nodeId);
    return slot < rack.midiRoutes.size() ? &rack.midiRoutes[slot] : nullptr;
}

bool setMidiInstrumentRoute(InstrumentRackState& rack, uint32_t nodeId,
    MidiInstrumentRoute route) noexcept
{
    const auto slot = midiOutRackSlotIndex(nodeId);
    if (slot >= rack.midiRoutes.size()) return false;
    route.channel = static_cast<uint8_t>(std::clamp<int>(route.channel, 1, 16));
    route.virtualSource = static_cast<uint8_t>(std::clamp<int>(
        route.virtualSource, 1, static_cast<int>(kMidiOutRackSlotCount)));
    if (route.kind == MidiInstrumentRouteKind::VirtualSource)
        route.destinationId = 0;
    rack.midiRoutes[slot] = route;
    return true;
}

std::size_t membraneParameterCount() noexcept { return kParameters.size(); }

const MembraneParameterDefinition* membraneParameter(
    std::size_t index) noexcept
{
    return index < kParameters.size() ? &kParameters[index] : nullptr;
}

const MembraneParameterDefinition* findMembraneParameter(
    uint32_t parameterId) noexcept
{
    for (const auto& parameter : kParameters) {
        if (parameter.parameterId == parameterId) return &parameter;
    }
    return nullptr;
}

std::size_t membraneParameterIndex(uint32_t parameterId) noexcept
{
    for (std::size_t index = 0u; index < kParameters.size(); ++index) {
        if (kParameters[index].parameterId == parameterId) return index;
    }
    return kParameters.size();
}

double membraneNativeFromNormalized(uint32_t parameterId,
    float normalized) noexcept
{
    const auto* parameter = findMembraneParameter(parameterId);
    if (!parameter) return 0.0;
    const double unit = std::clamp(std::isfinite(normalized)
            ? static_cast<double>(normalized) : 0.0,
        0.0, 1.0);
    double value = parameter->minimum
        + unit * (parameter->maximum - parameter->minimum);
    if (parameter->control == MembraneControlKind::Stepped)
        value = std::round(value);
    return value;
}

float membraneNormalizedFromNative(uint32_t parameterId,
    double nativeValue) noexcept
{
    const auto* parameter = findMembraneParameter(parameterId);
    if (!parameter || !(parameter->maximum > parameter->minimum))
        return 0.0f;
    const double finite = std::isfinite(nativeValue)
        ? nativeValue : parameter->defaultValue;
    return static_cast<float>(std::clamp(
        (finite - parameter->minimum)
            / (parameter->maximum - parameter->minimum),
        0.0, 1.0));
}

std::string_view membraneInstrumentRoleName(
    MembraneInstrumentRole role) noexcept
{
    switch (role) {
    case MembraneInstrumentRole::Kick: return "KICK";
    case MembraneInstrumentRole::SnareBody: return "SNARE BODY";
    case MembraneInstrumentRole::FloorTom: return "FLOOR TOM";
    case MembraneInstrumentRole::LowTom: return "LOW TOM";
    case MembraneInstrumentRole::HighTom: return "HIGH TOM";
    }
    return "MEMBRANE";
}

const NamedInstrumentPreset* membranePreset(std::size_t index) noexcept
{
    return index < kMembranePresets.size() ? &kMembranePresets[index]
                                           : nullptr;
}

std::size_t membranePresetIndex(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept
{
    if (nodeId >= rack.slots.size()) return kMembranePresetCount;
    for (std::size_t index = 0u; index < kMembranePresetCount; ++index) {
        if (rack.slots[nodeId].basePatch.normalized
            == membranePresetPatch(index).normalized) return index;
    }
    return kMembranePresetCount;
}

bool applyMembranePreset(InstrumentRackState& rack, uint32_t nodeId,
    std::size_t presetIndex) noexcept
{
    if (nodeId >= rack.slots.size()
        || presetIndex >= kMembranePresetCount) return false;
    rack.slots[nodeId].basePatch = membranePresetPatch(presetIndex);
    return true;
}

InstrumentRackState makeDefaultInstrumentRack()
{
    InstrumentRackState rack;
    rack.instruments[0u] = kDefaultInstruments[0u];
    rack.instruments[1u] = kDefaultInstruments[kStereoSamplerInstrumentNode];
    rack.instruments[2u] = kDefaultInstruments[kMidiOutInstrumentNode];
    for (auto& patch : rack.sn76489Patches)
        patch = defaultSn76489Patch();
    for (auto& patch : rack.ym2151Patches)
        patch = ym2151PresetPatch(0u);
    for (std::size_t index = 0u; index < rack.daisyDrumPatches.size();
         ++index) {
        const auto nodeId = kDaisyAnalogBassDrumInstrumentNode
            + static_cast<uint32_t>(index);
        rack.daisyDrumPatches[index] = daisyDrumPresetPatch(
            daisyDrumKindForNode(nodeId), 0u);
    }
    for (std::size_t slot = 0u; slot < rack.midiRoutes.size(); ++slot) {
        rack.midiRoutes[slot].kind = MidiInstrumentRouteKind::VirtualSource;
        rack.midiRoutes[slot].destinationId = 0;
        rack.midiRoutes[slot].virtualSource = static_cast<uint8_t>(slot + 1u);
        rack.midiRoutes[slot].channel = static_cast<uint8_t>(slot + 1u);
    }
    for (std::size_t slot = 0u; slot < rack.samplerSlots.size(); ++slot) {
        rack.samplerSlots[slot].nodeId = stereoSamplerNodeForRackSlot(slot);
        rack.samplerSlots[slot].baseNote = 36u;
    }
    constexpr std::array<MembraneInstrumentRole,
        kMembraneRackSlotCount> roles {{
        MembraneInstrumentRole::Kick,
        MembraneInstrumentRole::Kick,
        MembraneInstrumentRole::Kick,
        MembraneInstrumentRole::Kick,
        MembraneInstrumentRole::Kick,
    }};
    for (std::size_t index = 0u; index < rack.slots.size(); ++index) {
        rack.slots[index].nodeId = static_cast<uint32_t>(index);
        rack.slots[index].role = roles[index];
        rack.slots[index].basePatch = rolePatch(roles[index]);
    }
    return rack;
}

bool setMembraneBaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept
{
    const auto parameterIndex = membraneParameterIndex(parameterId);
    if (nodeId >= rack.slots.size()
        || parameterIndex >= kMembraneParameterCount) return false;
    rack.slots[nodeId].basePatch.normalized[parameterIndex] =
        std::clamp(std::isfinite(normalized) ? normalized : 0.0f,
            0.0f, 1.0f);
    return true;
}

float membraneBaseParameter(const InstrumentRackState& rack,
    uint32_t nodeId, uint32_t parameterId) noexcept
{
    const auto parameterIndex = membraneParameterIndex(parameterId);
    return nodeId < rack.slots.size()
            && parameterIndex < kMembraneParameterCount
        ? rack.slots[nodeId].basePatch.normalized[parameterIndex] : 0.0f;
}

} // namespace s3g::tracker
