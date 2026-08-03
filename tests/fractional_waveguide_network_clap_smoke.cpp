#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kChannels = 16u;
constexpr uint32_t kFrames = 256u;
constexpr double kSampleRate = 48000.0;
constexpr clap_id kOrderParamId = 1u;
constexpr clap_id kSpeedParamId = 2u;
constexpr clap_id kActuatorNodeParamId = 8u;
constexpr clap_id kStrikeGainParamId = 10u;
constexpr clap_id kOutputGainParamId = 11u;
constexpr clap_id kSelfExcitationGainParamId = 12u;
constexpr clap_id kSelfExcitationRateParamId = 13u;
constexpr clap_id kEuclideanStepsParamId = 14u;
constexpr clap_id kEuclideanPulsesFirstParamId = 15u;
constexpr clap_id kEuclideanRotationFirstParamId = 23u;
constexpr clap_id kExciterTypeParamId = 31u;
constexpr clap_id kSustainedExcitationParamId = 32u;
constexpr clap_id kExciterCharacterParamId = 33u;
constexpr clap_id kDispersionParamId = 34u;
constexpr clap_id kMidiModeParamId = 35u;
constexpr clap_id kMidiTransposeParamId = 36u;
constexpr clap_id kMidiAttackParamId = 37u;
constexpr clap_id kMidiReleaseParamId = 38u;
constexpr clap_id kMidiVelocityParamId = 39u;
constexpr clap_id kNodeDirectivityFirstParamId = 40u;
constexpr clap_id kSequencerScaleParamId = 48u;
constexpr clap_id kSequencerNoteCountParamId = 49u;

struct HostContext {
    clap_host_t host {};
    clap_host_tail_t tail {};
    uint32_t tailChanges = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (id && std::strcmp(id, CLAP_EXT_TAIL) == 0) {
        return &hostContext(host)->tail;
    }
    return nullptr;
}

void hostRequest(const clap_host_t*) {}

void hostTailChanged(const clap_host_t* host)
{
    ++hostContext(host)->tailChanges;
}

struct EventList {
    std::vector<const clap_event_header_t*> events;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self =
                static_cast<const EventList*>(list->ctx);
            return self
                ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self =
                static_cast<const EventList*>(list->ctx);
            return self && index < self->events.size()
                ? self->events[index] : nullptr;
        },
    };
    std::array<clap_event_param_value_t, 32u> params {};
    std::array<clap_event_note_t, 16u> notes {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;

    void addParam(clap_id id, double value, uint32_t time = 0u)
    {
        auto& event = params[paramCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events.push_back(&event.header);
    }

    void addNote(int16_t key, double velocity, uint32_t time)
    {
        auto& event = notes[noteCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = key;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        events.push_back(&event.header);
    }

    void addNoteOff(int16_t key, uint32_t time)
    {
        auto& event = notes[noteCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_OFF;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = key;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 0.0;
        events.push_back(&event.header);
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 9u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry :
             std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

struct AudioBlock {
    std::array<float, kFrames> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, 1u> inputPointers {};
    std::array<float*, kChannels> outputPointers {};
    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};

    AudioBlock()
    {
        inputPointers[0] = input.data();
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            outputPointers[channel] = output[channel].data();
        }
        inputBuffer.data32 = inputPointers.data();
        inputBuffer.channel_count = 1u;
        outputBuffer.data32 = outputPointers.data();
        outputBuffer.channel_count = kChannels;
    }

    void clear()
    {
        input.fill(0.0f);
        for (auto& channel : output) channel.fill(0.0f);
    }
};

clap_process_status runBlock(const clap_plugin_t* plugin,
    AudioBlock& audio, const clap_input_events_t* events)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &audio.inputBuffer;
    process.audio_outputs = &audio.outputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process);
}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double expected)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value)
        && std::abs(value - expected) <= 1.0e-6;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_fractional_waveguide_network_clap_smoke "
            << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve waveguide encoder binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load waveguide encoder: "
            << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    HostContext hostContext {};
    hostContext.tail.changed = hostTailChanged;
    hostContext.host.clap_version = CLAP_VERSION_INIT;
    hostContext.host.host_data = &hostContext;
    hostContext.host.name = "Waveguide encoder smoke";
    hostContext.host.vendor = "s3g";
    hostContext.host.url = "https://github.com/s3g/s3g-dsp";
    hostContext.host.version = "1";
    hostContext.host.get_extension = hostGetExtension;
    hostContext.host.request_restart = hostRequest;
    hostContext.host.request_process = hostRequest;
    hostContext.host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(
            factory, &hostContext.host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* audioPorts = ok
        ? static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS))
        : nullptr;
    const auto* notePorts = ok
        ? static_cast<const clap_plugin_note_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS))
        : nullptr;
    const auto* params = ok
        ? static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS))
        : nullptr;
    const auto* state = ok
        ? static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE))
        : nullptr;
    const auto* tail = ok
        ? static_cast<const clap_plugin_tail_t*>(
            plugin->get_extension(plugin, CLAP_EXT_TAIL))
        : nullptr;
    clap_audio_port_info_t inputInfo {};
    clap_audio_port_info_t outputInfo {};
    clap_note_port_info_t noteInfo {};
    ok = ok && audioPorts && notePorts && params && state && tail
        && audioPorts->count(plugin, true) == 1u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, true, &inputInfo)
        && inputInfo.channel_count == 1u
        && inputInfo.port_type
            && std::strcmp(inputInfo.port_type, CLAP_PORT_MONO) == 0
        && audioPorts->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && outputInfo.port_type
            && std::strcmp(outputInfo.port_type, CLAP_PORT_AMBISONIC) == 0
        && notePorts->count(plugin, true) == 1u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && std::strcmp(noteInfo.name, "Waveguide MIDI / Strike In") == 0
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && params->count(plugin) == 49u;

    if (ok) {
        clap_param_info_t order {};
        clap_param_info_t selfExcitation {};
        clap_param_info_t selfExcitationRate {};
        clap_param_info_t euclideanSteps {};
        clap_param_info_t nodeOnePulses {};
        clap_param_info_t nodeOneRotation {};
        clap_param_info_t exciter {};
        clap_param_info_t sustain {};
        clap_param_info_t dispersion {};
        clap_param_info_t midiMode {};
        clap_param_info_t midiRelease {};
        clap_param_info_t midiVelocity {};
        clap_param_info_t nodeOneDirectivity {};
        clap_param_info_t sequencerScale {};
        clap_param_info_t sequencerNoteCount {};
        ok = params->get_info(plugin, 0u, &order)
            && order.id == kOrderParamId
            && (order.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && order.min_value == 1.0 && order.max_value == 3.0
            && params->get_info(plugin, 11u, &selfExcitation)
            && selfExcitation.id == kSelfExcitationGainParamId
            && std::strcmp(selfExcitation.name, "Self Excitation") == 0
            && selfExcitation.min_value == 0.0
            && selfExcitation.max_value == 1.0
            && selfExcitation.default_value > 0.0
            && params->get_info(plugin, 12u, &selfExcitationRate)
            && selfExcitationRate.id == kSelfExcitationRateParamId
            && std::strcmp(
                selfExcitationRate.name, "Self Excitation Rate") == 0
            && selfExcitationRate.min_value == 0.05
            && selfExcitationRate.max_value == 12.0
            && params->get_info(plugin, 13u, &euclideanSteps)
            && euclideanSteps.id == kEuclideanStepsParamId
            && (euclideanSteps.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && euclideanSteps.min_value == 4.0
            && euclideanSteps.max_value == 32.0
            && params->get_info(plugin, 14u, &nodeOnePulses)
            && nodeOnePulses.id == kEuclideanPulsesFirstParamId
            && std::strcmp(nodeOnePulses.name,
                "Node 1 Euclidean Pulses") == 0
            && (nodeOnePulses.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && params->get_info(plugin, 22u, &nodeOneRotation)
            && nodeOneRotation.id == kEuclideanRotationFirstParamId
            && std::strcmp(nodeOneRotation.name,
                "Node 1 Euclidean Rotation") == 0
            && (nodeOneRotation.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && params->get_info(plugin, 30u, &exciter)
            && exciter.id == kExciterTypeParamId
            && std::strcmp(exciter.name, "Sustained Exciter") == 0
            && exciter.min_value == 0.0 && exciter.max_value == 3.0
            && (exciter.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && params->get_info(plugin, 31u, &sustain)
            && sustain.id == kSustainedExcitationParamId
            && std::strcmp(sustain.name, "Sustain Drive") == 0
            && params->get_info(plugin, 33u, &dispersion)
            && dispersion.id == kDispersionParamId
            && std::strcmp(dispersion.name, "Waveguide Dispersion") == 0
            && params->get_info(plugin, 34u, &midiMode)
            && midiMode.id == kMidiModeParamId
            && std::strcmp(midiMode.name, "MIDI Note Layer") == 0
            && (midiMode.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && midiMode.min_value == 0.0 && midiMode.max_value == 3.0
            && params->get_info(plugin, 37u, &midiRelease)
            && midiRelease.id == kMidiReleaseParamId
            && midiRelease.min_value == 5.0
            && midiRelease.max_value == 5000.0
            && params->get_info(plugin, 38u, &midiVelocity)
            && midiVelocity.id == kMidiVelocityParamId
            && midiVelocity.default_value == 0.85
            && params->get_info(plugin, 39u, &nodeOneDirectivity)
            && nodeOneDirectivity.id == kNodeDirectivityFirstParamId
            && std::strcmp(
                nodeOneDirectivity.name, "Node 1 Directivity Mask") == 0
            && nodeOneDirectivity.min_value == 0.0
            && nodeOneDirectivity.max_value == 1.0
            && std::fabs(
                nodeOneDirectivity.default_value - 0.55) < 0.000001
            && params->get_info(plugin, 47u, &sequencerScale)
            && sequencerScale.id == kSequencerScaleParamId
            && std::strcmp(sequencerScale.name, "Sequencer Scale") == 0
            && sequencerScale.min_value == 0.0
            && sequencerScale.max_value == 7.0
            && (sequencerScale.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && params->get_info(plugin, 48u, &sequencerNoteCount)
            && sequencerNoteCount.id == kSequencerNoteCountParamId
            && std::strcmp(sequencerNoteCount.name,
                "Sequencer Note Variations") == 0
            && sequencerNoteCount.min_value == 1.0
            && sequencerNoteCount.max_value == 8.0
            && sequencerNoteCount.default_value == 1.0
            && (sequencerNoteCount.flags & CLAP_PARAM_IS_STEPPED) != 0u;
    }
    ok = ok
        && plugin->activate(plugin, kSampleRate, kFrames, kFrames)
        && plugin->start_processing(plugin)
        && tail->get(plugin) >= 0x7fffffffu;

    AudioBlock audio;
    if (ok) {
        audio.clear();
        ok = runBlock(plugin, audio, nullptr) == CLAP_PROCESS_CONTINUE;
        double autonomousEnergy = 0.0;
        double autonomousHigherOrder = 0.0;
        bool finite = true;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (float sample : audio.output[channel]) {
                finite = finite && std::isfinite(sample);
                autonomousEnergy += std::abs(sample);
                if (channel >= 4u) autonomousHigherOrder += std::abs(sample);
            }
        }
        ok = finite && autonomousEnergy > 1.0e-4
            && autonomousHigherOrder > 1.0e-5;
    }

    if (ok) {
        plugin->reset(plugin);
        audio.clear();
        EventList events;
        events.addParam(kSelfExcitationGainParamId, 0.0);
        events.addNote(62, 0.8, 17u);
        ok = runBlock(plugin, audio, &events.input)
            == CLAP_PROCESS_CONTINUE;
        double before = 0.0;
        double after = 0.0;
        double higherOrder = 0.0;
        float peak = 0.0f;
        bool finite = true;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float sample = audio.output[channel][frame];
                finite = finite && std::isfinite(sample);
                peak = std::max(peak, std::abs(sample));
                if (frame < 17u) before += std::abs(sample);
                else after += std::abs(sample);
                if (channel >= 4u) higherOrder += std::abs(sample);
            }
        }
        ok = finite && before < 1.0e-8
            && after > 1.0e-4 && higherOrder > 1.0e-5
            && peak <= 0.892f;
    }

    if (ok) {
        plugin->reset(plugin);
        audio.clear();
        EventList events;
        events.addParam(kOrderParamId, 1.0);
        events.addNote(60, 0.8, 0u);
        ok = runBlock(plugin, audio, &events.input)
            == CLAP_PROCESS_CONTINUE;
        double firstOrder = 0.0;
        double inactive = 0.0;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (float sample : audio.output[channel]) {
                if (channel < 4u) firstOrder += std::abs(sample);
                else inactive += std::abs(sample);
            }
        }
        ok = firstOrder > 1.0e-5 && inactive < 1.0e-8;
    }

    if (ok) {
        EventList clearPulses;
        for (uint32_t node = 0u; node < 8u; ++node) {
            clearPulses.addParam(
                kEuclideanPulsesFirstParamId + node, 0.0);
        }
        params->flush(plugin, &clearPulses.input, nullptr);
        EventList enable;
        enable.addParam(kSelfExcitationGainParamId, 0.32);
        params->flush(plugin, &enable.input, nullptr);
        ok = tail->get(plugin) < 0x7fffffffu;
        EventList restorePulse;
        restorePulse.addParam(kEuclideanPulsesFirstParamId, 5.0);
        params->flush(plugin, &restorePulse.input, nullptr);
        ok = ok && tail->get(plugin) >= 0x7fffffffu;
    }

    if (ok) {
        // The Euclidean SELF source must articulate every physical exciter,
        // without relying on the continuous Sustain control.
        EventList triggeredSetup;
        triggeredSetup.addParam(kSelfExcitationGainParamId, 0.72);
        triggeredSetup.addParam(kSelfExcitationRateParamId, 12.0);
        triggeredSetup.addParam(kEuclideanStepsParamId, 4.0);
        for (uint32_t node = 0u; node < 8u; ++node) {
            triggeredSetup.addParam(
                kEuclideanPulsesFirstParamId + node,
                node == 0u ? 4.0 : 0.0);
        }
        triggeredSetup.addParam(kSustainedExcitationParamId, 0.0);
        triggeredSetup.addParam(kExciterCharacterParamId, 0.58);
        params->flush(plugin, &triggeredSetup.input, nullptr);
        for (uint32_t exciter = 1u; exciter <= 3u && ok; ++exciter) {
            EventList selectExciter;
            selectExciter.addParam(kExciterTypeParamId, exciter);
            params->flush(plugin, &selectExciter.input, nullptr);
            plugin->reset(plugin);
            double gestureEnergy = 0.0;
            for (uint32_t block = 0u; block < 16u && ok; ++block) {
                audio.clear();
                ok = runBlock(plugin, audio, nullptr)
                    == CLAP_PROCESS_CONTINUE;
                for (const auto& channel : audio.output) {
                    for (const float sample : channel) {
                        ok = ok && std::isfinite(sample);
                        gestureEnergy +=
                            static_cast<double>(sample) * sample;
                    }
                }
            }
            ok = ok && gestureEnergy > 1.0e-7;
        }
    }

    if (ok) {
        // A dense lane schedules surplus pulses inside the step instead of
        // clamping to one hit. At 12 Hz, 8 pulses / 4 steps puts the second
        // ratchet exactly 2000 samples after the otherwise identical hit.
        EventList ratchetSetup;
        ratchetSetup.addParam(kSelfExcitationGainParamId, 0.72);
        ratchetSetup.addParam(kSelfExcitationRateParamId, 12.0);
        ratchetSetup.addParam(kEuclideanStepsParamId, 4.0);
        for (uint32_t node = 0u; node < 8u; ++node) {
            ratchetSetup.addParam(
                kEuclideanPulsesFirstParamId + node, 0.0);
        }
        ratchetSetup.addParam(kExciterTypeParamId, 0.0);
        ratchetSetup.addParam(kSustainedExcitationParamId, 0.0);
        params->flush(plugin, &ratchetSetup.input, nullptr);

        constexpr uint32_t ratchetBlocks = 9u;
        std::array<float, kFrames * ratchetBlocks> sparse {};
        std::array<float, kFrames * ratchetBlocks> dense {};
        const auto renderLane = [&](double pulses, auto& rendered) {
            EventList lane;
            lane.addParam(kEuclideanPulsesFirstParamId, pulses);
            params->flush(plugin, &lane.input, nullptr);
            plugin->reset(plugin);
            for (uint32_t block = 0u; block < ratchetBlocks; ++block) {
                audio.clear();
                if (runBlock(plugin, audio, nullptr)
                    != CLAP_PROCESS_CONTINUE) {
                    return false;
                }
                std::copy(audio.output[0].begin(), audio.output[0].end(),
                    rendered.begin() + block * kFrames);
            }
            return std::all_of(rendered.begin(), rendered.end(),
                [](float sample) { return std::isfinite(sample); });
        };
        ok = renderLane(4.0, sparse) && renderLane(8.0, dense);
        double beforeRatchet = 0.0;
        double afterRatchet = 0.0;
        for (uint32_t frame = 0u; frame < sparse.size(); ++frame) {
            const double delta =
                static_cast<double>(sparse[frame]) - dense[frame];
            if (frame < 2000u) beforeRatchet += delta * delta;
            else afterRatchet += delta * delta;
        }
        ok = ok && beforeRatchet < 1.0e-14 && afterRatchet > 1.0e-8;
    }

    if (ok) {
        EventList sustained;
        sustained.addParam(kSelfExcitationGainParamId, 0.0);
        for (uint32_t node = 0u; node < 8u; ++node) {
            sustained.addParam(kEuclideanPulsesFirstParamId + node, 0.0);
        }
        sustained.addParam(kExciterTypeParamId, 2.0);
        sustained.addParam(kSustainedExcitationParamId, 0.62);
        sustained.addParam(kExciterCharacterParamId, 0.54);
        sustained.addParam(kDispersionParamId, 0.31);
        params->flush(plugin, &sustained.input, nullptr);
        plugin->reset(plugin);
        double lateEnergy = 0.0;
        for (uint32_t block = 0u; block < 80u && ok; ++block) {
            audio.clear();
            ok = runBlock(plugin, audio, nullptr) == CLAP_PROCESS_CONTINUE;
            if (block >= 72u) {
                for (const auto& channel : audio.output) {
                    for (float sample : channel) {
                        lateEnergy += static_cast<double>(sample) * sample;
                    }
                }
            }
        }
        ok = ok && lateEnergy > 1.0e-7
            && tail->get(plugin) >= 0x7fffffffu;
        EventList stopSustain;
        stopSustain.addParam(kSustainedExcitationParamId, 0.0);
        params->flush(plugin, &stopSustain.input, nullptr);
        ok = ok && tail->get(plugin) < 0x7fffffffu;
    }

    if (ok) {
        EventList midiSetup;
        midiSetup.addParam(kSelfExcitationGainParamId, 0.0);
        for (uint32_t node = 0u; node < 8u; ++node) {
            midiSetup.addParam(kEuclideanPulsesFirstParamId + node, 0.0);
        }
        midiSetup.addParam(kExciterTypeParamId, 2.0);
        midiSetup.addParam(kSustainedExcitationParamId, 0.58);
        midiSetup.addParam(kMidiModeParamId, 1.0);
        midiSetup.addParam(kMidiTransposeParamId, 0.0);
        midiSetup.addParam(kMidiAttackParamId, 0.5);
        midiSetup.addParam(kMidiReleaseParamId, 5.0);
        midiSetup.addParam(kMidiVelocityParamId, 1.0);
        params->flush(plugin, &midiSetup.input, nullptr);
        plugin->reset(plugin);
        ok = tail->get(plugin) < 0x7fffffffu;

        EventList noteOn;
        noteOn.addNote(69, 0.72, 17u);
        audio.clear();
        ok = ok && runBlock(plugin, audio, &noteOn.input)
            == CLAP_PROCESS_CONTINUE;
        double before = 0.0;
        double after = 0.0;
        for (const auto& channel : audio.output) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const double magnitude = std::abs(channel[frame]);
                if (frame < 17u) before += magnitude;
                else after += magnitude;
            }
        }
        ok = ok && before < 1.0e-8 && after > 1.0e-4;

        double heldEnergy = 0.0;
        for (uint32_t block = 0u; block < 24u && ok; ++block) {
            audio.clear();
            ok = runBlock(plugin, audio, nullptr) == CLAP_PROCESS_CONTINUE;
            if (block >= 16u) {
                for (const auto& channel : audio.output) {
                    for (float sample : channel) {
                        heldEnergy += static_cast<double>(sample) * sample;
                    }
                }
            }
        }
        EventList noteOff;
        noteOff.addNoteOff(69, 0u);
        audio.clear();
        ok = ok && runBlock(plugin, audio, &noteOff.input)
            == CLAP_PROCESS_CONTINUE;
        double releasedEnergy = 0.0;
        for (uint32_t block = 0u; block < 440u && ok; ++block) {
            audio.clear();
            ok = runBlock(plugin, audio, nullptr) == CLAP_PROCESS_CONTINUE;
            if (block >= 432u) {
                for (const auto& channel : audio.output) {
                    for (float sample : channel) {
                        releasedEnergy += static_cast<double>(sample) * sample;
                    }
                }
            }
        }
        ok = ok && heldEnergy > 1.0e-7
            && releasedEnergy < heldEnergy * 0.25;
    }

    if (ok) {
        EventList allocationSetup;
        allocationSetup.addParam(kSelfExcitationGainParamId, 0.0);
        allocationSetup.addParam(kExciterTypeParamId, 0.0);
        allocationSetup.addParam(kSustainedExcitationParamId, 0.0);
        allocationSetup.addParam(kMidiModeParamId, 2.0);
        allocationSetup.addParam(kMidiVelocityParamId, 1.0);
        params->flush(plugin, &allocationSetup.input, nullptr);
        plugin->reset(plugin);

        EventList firstTwoNotes;
        firstTwoNotes.addNote(60, 0.0001, 0u);
        firstTwoNotes.addNote(64, 1.0, 32u);
        audio.clear();
        ok = runBlock(plugin, audio, &firstTwoNotes.input)
            == CLAP_PROCESS_CONTINUE
            && audio.output[0][0] > 0.0f
            && audio.output[1][0] < 0.0f
            && audio.output[2][0] < 0.0f
            && audio.output[3][0] < 0.0f
            && audio.output[0][32] > 0.0f
            && audio.output[1][32] < 0.0f
            && audio.output[2][32] < 0.0f
            && audio.output[3][32] > 0.0f;
    }

    if (ok) {
        // Off is the percussive model, not a MIDI bypass. With the broadband
        // strike muted, poly MIDI must still inject a finite pitched body.
        EventList percussiveSetup;
        percussiveSetup.addParam(kExciterTypeParamId, 0.0);
        percussiveSetup.addParam(kSustainedExcitationParamId, 0.0);
        percussiveSetup.addParam(kStrikeGainParamId, 0.0);
        percussiveSetup.addParam(kMidiModeParamId, 2.0);
        percussiveSetup.addParam(kMidiAttackParamId, 0.5);
        percussiveSetup.addParam(kMidiReleaseParamId, 180.0);
        percussiveSetup.addParam(kMidiVelocityParamId, 1.0);
        params->flush(plugin, &percussiveSetup.input, nullptr);

        std::array<float, kFrames> lowNote {};
        std::array<float, kFrames> highNote {};
        const auto renderNote = [&](int16_t key,
                                    std::array<float, kFrames>& rendered) {
            plugin->reset(plugin);
            EventList note;
            note.addNote(key, 0.8, 0u);
            audio.clear();
            if (runBlock(plugin, audio, &note.input)
                != CLAP_PROCESS_CONTINUE) {
                return false;
            }
            rendered = audio.output[0];
            return std::all_of(rendered.begin(), rendered.end(),
                [](float sample) { return std::isfinite(sample); });
        };
        ok = renderNote(48, lowNote) && renderNote(72, highNote);
        double lowEnergy = 0.0;
        double highEnergy = 0.0;
        double pitchDifference = 0.0;
        for (uint32_t frame = 32u; frame < kFrames; ++frame) {
            lowEnergy += static_cast<double>(lowNote[frame]) * lowNote[frame];
            highEnergy +=
                static_cast<double>(highNote[frame]) * highNote[frame];
            const double delta = static_cast<double>(lowNote[frame])
                - highNote[frame];
            pitchDifference += delta * delta;
        }
        ok = ok && lowEnergy > 1.0e-8 && highEnergy > 1.0e-8
            && pitchDifference > 1.0e-8;
        EventList restoreStrike;
        restoreStrike.addParam(kStrikeGainParamId, 1.0);
        params->flush(plugin, &restoreStrike.input, nullptr);
    }

    if (ok) {
        EventList polySetup;
        polySetup.addParam(kOrderParamId, 3.0);
        polySetup.addParam(kExciterTypeParamId, 2.0);
        polySetup.addParam(kSustainedExcitationParamId, 0.55);
        polySetup.addParam(kMidiModeParamId, 2.0);
        polySetup.addParam(kMidiTransposeParamId, 0.0);
        polySetup.addParam(kMidiAttackParamId, 1.0);
        polySetup.addParam(kMidiReleaseParamId, 20.0);
        polySetup.addParam(kMidiVelocityParamId, 0.9);
        params->flush(plugin, &polySetup.input, nullptr);
        plugin->reset(plugin);

        constexpr std::array<int16_t, 8u> chord {{
            48, 52, 55, 59, 62, 65, 69, 72
        }};
        EventList notesOn;
        for (uint32_t note = 0u; note < chord.size(); ++note) {
            notesOn.addNote(chord[note], 0.55 + 0.05 * note, note * 8u);
        }
        audio.clear();
        ok = runBlock(plugin, audio, &notesOn.input)
            == CLAP_PROCESS_CONTINUE;
        double heldPolyEnergy = 0.0;
        double heldHigherOrder = 0.0;
        float polyPeak = 0.0f;
        for (uint32_t block = 0u; block < 80u && ok; ++block) {
            audio.clear();
            ok = runBlock(plugin, audio, nullptr) == CLAP_PROCESS_CONTINUE;
            if (block >= 72u) {
                for (uint32_t channel = 0u; channel < kChannels; ++channel) {
                    for (float sample : audio.output[channel]) {
                        ok = ok && std::isfinite(sample);
                        const double energy =
                            static_cast<double>(sample) * sample;
                        heldPolyEnergy += energy;
                        if (channel >= 4u) heldHigherOrder += energy;
                        polyPeak = std::max(polyPeak, std::abs(sample));
                    }
                }
            }
        }
        ok = ok && heldPolyEnergy > 1.0e-6
            && heldHigherOrder > 1.0e-8 && polyPeak <= 0.892f;

        EventList notesOff;
        for (int16_t note : chord) notesOff.addNoteOff(note, 0u);
        audio.clear();
        ok = ok && runBlock(plugin, audio, &notesOff.input)
            == CLAP_PROCESS_CONTINUE;
        double releasedPolyEnergy = 0.0;
        for (uint32_t block = 0u; block < 440u && ok; ++block) {
            audio.clear();
            ok = runBlock(plugin, audio, nullptr) == CLAP_PROCESS_CONTINUE;
            if (block >= 432u) {
                for (const auto& channel : audio.output) {
                    for (float sample : channel) {
                        releasedPolyEnergy +=
                            static_cast<double>(sample) * sample;
                    }
                }
            }
        }
        ok = ok && releasedPolyEnergy < heldPolyEnergy * 0.25;

        EventList randomMode;
        randomMode.addParam(kMidiModeParamId, 3.0);
        params->flush(plugin, &randomMode.input, nullptr);
        plugin->reset(plugin);
        EventList randomNotes;
        randomNotes.addNote(57, 0.8, 0u);
        randomNotes.addNote(60, 0.8, 16u);
        randomNotes.addNote(64, 0.8, 32u);
        randomNotes.addNote(67, 0.8, 48u);
        audio.clear();
        ok = ok && runBlock(plugin, audio, &randomNotes.input)
            == CLAP_PROCESS_CONTINUE
            && getParam(plugin, params, kMidiModeParamId, 3.0);
        double randomEnergy = 0.0;
        for (const auto& channel : audio.output) {
            for (float sample : channel) randomEnergy += std::abs(sample);
        }
        ok = ok && randomEnergy > 1.0e-4;
    }

    MemoryState saved;
    if (ok) {
        EventList changes;
        changes.addParam(kSpeedParamId, 271.5);
        changes.addParam(kActuatorNodeParamId, 6.0);
        changes.addParam(kOutputGainParamId, -18.0);
        changes.addParam(kSelfExcitationGainParamId, 0.47);
        changes.addParam(kSelfExcitationRateParamId, 2.25);
        changes.addParam(kEuclideanStepsParamId, 13.0);
        changes.addParam(kEuclideanPulsesFirstParamId + 5u, 7.0);
        changes.addParam(kEuclideanRotationFirstParamId + 5u, 9.0);
        changes.addParam(kExciterTypeParamId, 1.0);
        changes.addParam(kSustainedExcitationParamId, 0.56);
        changes.addParam(kExciterCharacterParamId, 0.43);
        changes.addParam(kDispersionParamId, 0.37);
        changes.addParam(kMidiModeParamId, 1.0);
        changes.addParam(kMidiTransposeParamId, -7.0);
        changes.addParam(kMidiAttackParamId, 14.0);
        changes.addParam(kMidiReleaseParamId, 850.0);
        changes.addParam(kMidiVelocityParamId, 0.63);
        changes.addParam(kNodeDirectivityFirstParamId + 5u, 0.87);
        changes.addParam(kSequencerScaleParamId, 5.0);
        changes.addParam(kSequencerNoteCountParamId, 8.0);
        params->flush(plugin, &changes.input, nullptr);
        clap_ostream_t stream { &saved, stateWrite };
        ok = state->save(plugin, &stream)
            && saved.bytes.size() == 400u
            && getParam(plugin, params, kSpeedParamId, 271.5)
            && getParam(plugin, params, kActuatorNodeParamId, 6.0)
            && getParam(plugin, params, kOutputGainParamId, -18.0)
            && getParam(plugin, params, kSelfExcitationGainParamId, 0.47)
            && getParam(plugin, params, kSelfExcitationRateParamId, 2.25)
            && getParam(plugin, params, kEuclideanStepsParamId, 13.0)
            && getParam(plugin, params,
                kEuclideanPulsesFirstParamId + 5u, 7.0)
            && getParam(plugin, params,
                kEuclideanRotationFirstParamId + 5u, 9.0)
            && getParam(plugin, params, kExciterTypeParamId, 1.0)
            && getParam(plugin, params, kSustainedExcitationParamId, 0.56)
            && getParam(plugin, params, kExciterCharacterParamId, 0.43)
            && getParam(plugin, params, kDispersionParamId, 0.37)
            && getParam(plugin, params, kMidiModeParamId, 1.0)
            && getParam(plugin, params, kMidiTransposeParamId, -7.0)
            && getParam(plugin, params, kMidiAttackParamId, 14.0)
            && getParam(plugin, params, kMidiReleaseParamId, 850.0)
            && getParam(plugin, params, kMidiVelocityParamId, 0.63)
            && getParam(plugin, params,
                kNodeDirectivityFirstParamId + 5u, 0.87)
            && getParam(plugin, params, kSequencerScaleParamId, 5.0)
            && getParam(plugin, params, kSequencerNoteCountParamId, 8.0)
            && hostContext.tailChanges > 0u;
    }
    if (ok) {
        EventList changes;
        changes.addParam(kSpeedParamId, 800.0);
        changes.addParam(kActuatorNodeParamId, 2.0);
        changes.addParam(kSelfExcitationGainParamId, 0.0);
        changes.addParam(kSelfExcitationRateParamId, 8.0);
        changes.addParam(kEuclideanStepsParamId, 24.0);
        changes.addParam(kEuclideanPulsesFirstParamId + 5u, 2.0);
        changes.addParam(kEuclideanRotationFirstParamId + 5u, 18.0);
        changes.addParam(kExciterTypeParamId, 3.0);
        changes.addParam(kSustainedExcitationParamId, 0.1);
        changes.addParam(kExciterCharacterParamId, 0.9);
        changes.addParam(kDispersionParamId, 0.8);
        changes.addParam(kMidiModeParamId, 0.0);
        changes.addParam(kMidiTransposeParamId, 12.0);
        changes.addParam(kMidiAttackParamId, 120.0);
        changes.addParam(kMidiReleaseParamId, 2200.0);
        changes.addParam(kMidiVelocityParamId, 0.1);
        changes.addParam(kNodeDirectivityFirstParamId + 5u, 0.05);
        changes.addParam(kSequencerScaleParamId, 0.0);
        changes.addParam(kSequencerNoteCountParamId, 1.0);
        params->flush(plugin, &changes.input, nullptr);
        MemoryState input = saved;
        clap_istream_t stream { &input, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params, kSpeedParamId, 271.5)
            && getParam(plugin, params, kActuatorNodeParamId, 6.0)
            && getParam(plugin, params, kOutputGainParamId, -18.0)
            && getParam(plugin, params, kSelfExcitationGainParamId, 0.47)
            && getParam(plugin, params, kSelfExcitationRateParamId, 2.25)
            && getParam(plugin, params, kEuclideanStepsParamId, 13.0)
            && getParam(plugin, params,
                kEuclideanPulsesFirstParamId + 5u, 7.0)
            && getParam(plugin, params,
                kEuclideanRotationFirstParamId + 5u, 9.0)
            && getParam(plugin, params, kExciterTypeParamId, 1.0)
            && getParam(plugin, params, kSustainedExcitationParamId, 0.56)
            && getParam(plugin, params, kExciterCharacterParamId, 0.43)
            && getParam(plugin, params, kDispersionParamId, 0.37)
            && getParam(plugin, params, kMidiModeParamId, 1.0)
            && getParam(plugin, params, kMidiTransposeParamId, -7.0)
            && getParam(plugin, params, kMidiAttackParamId, 14.0)
            && getParam(plugin, params, kMidiReleaseParamId, 850.0)
            && getParam(plugin, params, kMidiVelocityParamId, 0.63)
            && getParam(plugin, params,
                kNodeDirectivityFirstParamId + 5u, 0.87)
            && getParam(plugin, params, kSequencerScaleParamId, 5.0)
            && getParam(plugin, params, kSequencerNoteCountParamId, 8.0);
    }

    if (ok) {
        MemoryState legacy = saved;
        legacy.bytes.resize(384u);
        const uint32_t legacyVersion = 6u;
        std::memcpy(legacy.bytes.data(),
            &legacyVersion, sizeof(legacyVersion));
        clap_istream_t stream { &legacy, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params,
                kNodeDirectivityFirstParamId + 5u, 0.87)
            && getParam(plugin, params, kSequencerScaleParamId, 0.0)
            && getParam(plugin, params, kSequencerNoteCountParamId, 1.0);
    }

    if (ok) {
        MemoryState legacy = saved;
        legacy.bytes.resize(320u);
        const uint32_t legacyVersion = 5u;
        std::memcpy(legacy.bytes.data(),
            &legacyVersion, sizeof(legacyVersion));
        clap_istream_t stream { &legacy, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params, kMidiModeParamId, 1.0)
            && getParam(plugin, params,
                kNodeDirectivityFirstParamId + 5u, 0.55)
            && getParam(plugin, params, kSequencerScaleParamId, 0.0)
            && getParam(plugin, params, kSequencerNoteCountParamId, 1.0);
    }

    if (ok) {
        MemoryState legacy = saved;
        legacy.bytes.resize(280u);
        const uint32_t legacyVersion = 4u;
        std::memcpy(legacy.bytes.data(),
            &legacyVersion, sizeof(legacyVersion));
        clap_istream_t stream { &legacy, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params, kDispersionParamId, 0.37)
            && getParam(plugin, params, kMidiModeParamId, 0.0)
            && getParam(plugin, params, kMidiTransposeParamId, 0.0)
            && getParam(plugin, params, kMidiAttackParamId, 8.0)
            && getParam(plugin, params, kMidiReleaseParamId, 600.0)
            && getParam(plugin, params, kMidiVelocityParamId, 0.85)
            && getParam(plugin, params,
                kNodeDirectivityFirstParamId, 0.55);
    }

    if (ok) {
        MemoryState legacy = saved;
        legacy.bytes.resize(248u);
        const uint32_t legacyVersion = 3u;
        std::memcpy(legacy.bytes.data(),
            &legacyVersion, sizeof(legacyVersion));
        clap_istream_t stream { &legacy, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params, kEuclideanStepsParamId, 13.0)
            && getParam(plugin, params, kExciterTypeParamId, 0.0)
            && getParam(plugin, params, kSustainedExcitationParamId, 0.0)
            && getParam(plugin, params, kExciterCharacterParamId, 0.5)
            && getParam(plugin, params, kDispersionParamId, 0.0)
            && getParam(plugin, params, kMidiModeParamId, 0.0)
            && getParam(plugin, params, kMidiReleaseParamId, 600.0);
    }

    if (ok) {
        MemoryState legacy = saved;
        legacy.bytes.resize(112u);
        const uint32_t legacyVersion = 2u;
        std::memcpy(legacy.bytes.data(),
            &legacyVersion, sizeof(legacyVersion));
        clap_istream_t stream { &legacy, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params, kSpeedParamId, 271.5)
            && getParam(plugin, params, kSelfExcitationGainParamId, 0.47)
            && getParam(plugin, params, kSelfExcitationRateParamId, 2.25)
            && getParam(plugin, params, kEuclideanStepsParamId, 16.0)
            && getParam(plugin, params,
                kEuclideanPulsesFirstParamId + 5u, 3.0)
            && getParam(plugin, params,
                kEuclideanRotationFirstParamId + 5u, 5.0);
    }

    if (ok) {
        MemoryState legacy = saved;
        legacy.bytes.resize(96u);
        const uint32_t legacyVersion = 1u;
        std::memcpy(legacy.bytes.data(),
            &legacyVersion, sizeof(legacyVersion));
        clap_istream_t stream { &legacy, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params, kSpeedParamId, 271.5)
            && getParam(plugin, params, kActuatorNodeParamId, 6.0)
            && getParam(plugin, params, kOutputGainParamId, -18.0)
            && getParam(plugin, params, kSelfExcitationGainParamId, 0.0)
            && getParam(plugin, params, kSelfExcitationRateParamId, 0.8)
            && tail->get(plugin) < 0x7fffffffu;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Fractional waveguide encoder CLAP smoke failed\n";
        return 1;
    }
    std::cout << "Fractional waveguide encoder CLAP smoke passed\n";
    return 0;
}
