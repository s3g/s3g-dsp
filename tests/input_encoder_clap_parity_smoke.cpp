// Compare input encoders in independently built Cocoa and VSTGUI binaries. This
// deliberately does not include their implementation sources or duplicate their
// DSP math.
#include <algorithm>
#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool ok = true;
void check(bool result, const char *label) {
  if (!result) {
    std::cerr << label << '\n';
    ok = false;
  }
}
struct Stream {
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  clap_ostream_t out{
      this,
      [](const clap_ostream_t *s, const void *data, uint64_t size) -> int64_t {
        auto &v = static_cast<Stream *>(s->ctx)->bytes;
        size = std::min<uint64_t>(size, 7);
        const auto *b = static_cast<const uint8_t *>(data);
        v.insert(v.end(), b, b + size);
        return int64_t(size);
      }};
  clap_istream_t in{
      this, [](const clap_istream_t *s, void *data, uint64_t size) -> int64_t {
        auto &v = *static_cast<Stream *>(s->ctx);
        size = std::min<uint64_t>({size, 11, v.bytes.size() - v.pos});
        std::memcpy(data, v.bytes.data() + v.pos, size);
        v.pos += size;
        return int64_t(size);
      }};
};
struct Events {
  std::vector<clap_event_param_value_t> values;
  clap_input_events_t input{
      this,
      [](const clap_input_events_t *e) -> uint32_t {
        return uint32_t(static_cast<const Events *>(e->ctx)->values.size());
      },
      [](const clap_input_events_t *e,
         uint32_t i) -> const clap_event_header_t * {
        return &static_cast<const Events *>(e->ctx)->values[i].header;
      }};
  void add(clap_id id, double value) {
    clap_event_param_value_t e{};
    e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE,
                0};
    e.param_id = id;
    e.value = value;
    e.note_id = -1;
    e.port_index = e.channel = e.key = -1;
    values.push_back(e);
  }
};
struct Module {
  void *handle = nullptr;
  const clap_plugin_entry_t *entry = nullptr;
  const clap_plugin_t *plugin = nullptr;
  const clap_plugin_params_t *params = nullptr;
  const clap_plugin_state_t *state = nullptr;
  const clap_plugin_audio_ports_t *ports = nullptr;
  std::vector<clap_param_info_t> infos;
  clap_host_t host{};
  bool open(const char *path, const char *id) {
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Cocoa/VSTGUI parity";
    host.vendor = "s3g";
    host.version = "1";
    host.url = "";
    host.get_extension = [](const clap_host_t *, const char *) -> const void * {
      return nullptr;
    };
    host.request_process = host.request_callback =
        host.request_restart = [](const clap_host_t *) {};
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      std::cerr << dlerror() << '\n';
      return false;
    }
    entry =
        static_cast<const clap_plugin_entry_t *>(dlsym(handle, "clap_entry"));
    if (!entry || !entry->init(path))
      return false;
    const auto *factory = static_cast<const clap_plugin_factory_t *>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory)
      return false;
    plugin = factory->create_plugin(factory, &host, id);
    if (!plugin || !plugin->init(plugin))
      return false;
    params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    ports = static_cast<const clap_plugin_audio_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (!params || !state || !ports)
      return false;
    for (unsigned i = 0; i < params->count(plugin); ++i) {
      clap_param_info_t info{};
      if (!params->get_info(plugin, i, &info))
        return false;
      infos.push_back(info);
    }
    return true;
  }
  ~Module() {
    if (plugin)
      plugin->destroy(plugin);
    if (entry)
      entry->deinit();
    if (handle)
      dlclose(handle);
  }
};
void compareParams(Module &a, Module &b) {
  for (const auto &info : a.infos) {
    double x = 0., y = 0.;
    check(a.params->get_value(a.plugin, info.id, &x) &&
              b.params->get_value(b.plugin, info.id, &y) && std::isfinite(x) &&
              std::isfinite(y) && std::abs(x - y) < 1.e-5,
          info.name);
  }
}
void exchangeState(Module &a, Module &b) {
  Stream stream;
  check(a.state->save(a.plugin, &stream.out), "chunked state save");
  check(b.state->load(b.plugin, &stream.in), "cross-build chunked state load");
  if (std::strcmp(a.plugin->desc->id,
                  "org.s3g.s3g-dsp.ambi-encoder-membrane-kick-16") == 0) {
    // Trigger is a momentary action deliberately omitted from project state.
    // Match the original state loader's gate release on the source too.
    Events release;
    release.add(20u, 0.);
    a.params->flush(a.plugin, &release.input, nullptr);
  }
  compareParams(a, b);
  std::vector<double> before;
  for (const auto &i : b.infos) {
    double value = 0.;
    b.params->get_value(b.plugin, i.id, &value);
    before.push_back(value);
  }
  // Every truncation must fail transactionally, including the final byte.
  const auto bytes = stream.bytes;
  for (size_t length :
       {size_t(0), size_t(3), bytes.size() / 2, bytes.size() - 1}) {
    stream.bytes.assign(bytes.begin(), bytes.begin() + length);
    stream.pos = 0;
    check(!b.state->load(b.plugin, &stream.in), "truncated state rejected");
    for (size_t i = 0; i < b.infos.size(); ++i) {
      double value = 0.;
      b.params->get_value(b.plugin, b.infos[i].id, &value);
      check(value == before[i],
            "failed state load leaves parameters unchanged");
    }
  }
}
template <typename Sample>
void compareAudio(Module &a, Module &b, unsigned scene) {
  clap_audio_port_info_t port{};
  const bool hasInput = a.ports->count(a.plugin, true) > 0;
  if (hasInput)
    check(a.ports->get(a.plugin, 0, true, &port), "input port");
  const unsigned inputs = hasInput ? port.channel_count : 0u;
  check(a.ports->get(a.plugin, 0, false, &port), "output port");
  const unsigned outputs = port.channel_count, frames = 64;
  std::vector<std::vector<Sample>> in(inputs, std::vector<Sample>(frames)),
      ao(outputs, std::vector<Sample>(frames)),
      bo(outputs, std::vector<Sample>(frames));
  std::vector<Sample *> ip(inputs), ap(outputs), bp(outputs);
  for (unsigned c = 0; c < inputs; ++c)
    ip[c] = in[c].data();
  for (unsigned c = 0; c < outputs; ++c) {
    ap[c] = ao[c].data();
    bp[c] = bo[c].data();
  }
  clap_audio_buffer_t ib{}, ab{}, bb{};
  ib.channel_count = inputs;
  ab.channel_count = bb.channel_count = outputs;
  if constexpr (std::is_same_v<Sample, float>) {
    ib.data32 = ip.data();
    ab.data32 = ap.data();
    bb.data32 = bp.data();
  } else {
    ib.data64 = ip.data();
    ab.data64 = ap.data();
    bb.data64 = bp.data();
  }
  bool aa = a.plugin->activate(a.plugin, 48000., 1, frames),
       ba = b.plugin->activate(b.plugin, 48000., 1, frames);
  check(aa && ba, "activate both builds");
  if (!aa || !ba)
    return;
  // The old Cloud/Path prepare() reset edited geometry. The portable port
  // deliberately preserves it; compare DSP with the same post-activation
  // scene, then cover preservation separately in the canvas tests.
  Stream sceneState;
  check(a.state->save(a.plugin, &sceneState.out) &&
            b.state->load(b.plugin, &sceneState.in),
        "same post-activation scene");
  if (std::strcmp(a.plugin->desc->id,
                  "org.s3g.s3g-dsp.ambi-encoder-membrane-kick-16") == 0) {
    Events release;
    release.add(20u, 0.);
    a.params->flush(a.plugin, &release.input, nullptr);
  }
  check(a.plugin->start_processing(a.plugin) &&
            b.plugin->start_processing(b.plugin),
        "start both builds");
  double maximumError = 0., maximumSignal = 0.;
  for (unsigned block = 0; block < 80; ++block) {
    for (unsigned c = 0; c < inputs; ++c)
      for (unsigned f = 0; f < frames; ++f)
        in[c][f] =
            Sample(.015 * std::sin((block * frames + f) * (.009 + c * .0001) +
                                   c * .27));
    Events automation;
    if (block == 20 || block == 50) {
      for (size_t i = 0; i < a.infos.size(); ++i) {
        const auto &info = a.infos[i];
        if (info.flags & CLAP_PARAM_IS_READONLY)
          continue;
        const double n = double((i * 7 + block + scene * 11) % 79) / 79.;
        double value = info.min_value + n * (info.max_value - info.min_value);
        if (info.flags & CLAP_PARAM_IS_STEPPED)
          value = std::round(value);
        automation.add(info.id, value);
      }
    }
    clap_process_t proc{};
    proc.frames_count = frames;
    proc.audio_inputs = &ib;
    proc.audio_outputs = &ab;
    proc.audio_inputs_count = hasInput ? 1u : 0u;
    proc.audio_outputs_count = 1;
    clap_event_note_t note{};
    note.header = {sizeof(note), 0, CLAP_CORE_EVENT_SPACE_ID,
                   uint16_t(block == 1 || block == 25 || block == 55
                                ? CLAP_EVENT_NOTE_ON
                                : CLAP_EVENT_NOTE_OFF),
                   0};
    note.note_id = -1;
    note.port_index = 0;
    note.channel = 0;
    note.key = 60;
    note.velocity = .7;
    clap_input_events_t notes{
        &note, [](const clap_input_events_t *) -> uint32_t { return 1; },
        [](const clap_input_events_t *list,
           uint32_t) -> const clap_event_header_t * {
          return &static_cast<const clap_event_note_t *>(list->ctx)->header;
        }};
    const auto *notePorts = static_cast<const clap_plugin_note_ports_t *>(
        a.plugin->get_extension(a.plugin, CLAP_EXT_NOTE_PORTS));
    if (notePorts && notePorts->count(a.plugin, true) &&
        (block == 1 || block == 19 || block == 25 || block == 49 ||
         block == 55 || block == 79))
      proc.in_events = &notes;
    // Speaker rebuilds matrices asynchronously. Settle both workers before
    // comparing sample output; scheduling differences are not DSP differences.
    a.params->flush(a.plugin, &automation.input, nullptr);
    b.params->flush(b.plugin, &automation.input, nullptr);
    compareParams(a, b);
    check(a.plugin->process(a.plugin, &proc) != CLAP_PROCESS_ERROR,
          "Cocoa audio");
    proc.audio_outputs = &bb;
    check(b.plugin->process(b.plugin, &proc) != CLAP_PROCESS_ERROR,
          "VSTGUI audio");
    for (unsigned c = 0; c < outputs; ++c)
      for (unsigned f = 0; f < frames; ++f) {
        check(std::isfinite(ao[c][f]) && std::isfinite(bo[c][f]),
              "finite output");
        maximumError = std::max(maximumError,
                                std::abs(double(ao[c][f]) - double(bo[c][f])));
        maximumSignal = std::max(maximumSignal, std::abs(double(ao[c][f])));
      }
  }
  if (maximumError > 1.e-6)
    std::cerr << "audio max error " << maximumError << '\n';
  check(maximumError <= 1.e-6, "audio matches retained Cocoa build");
  if (scene == 0)
    check(maximumSignal > 1.e-9, "default scene actually produces audio");
  std::cout << "scene " << scene << " maximum sample difference "
            << maximumError << "; signal peak " << maximumSignal << '\n';
  a.plugin->stop_processing(a.plugin);
  b.plugin->stop_processing(b.plugin);
  a.plugin->deactivate(a.plugin);
  b.plugin->deactivate(b.plugin);
  compareParams(a, b);
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: parity cocoa-binary vstgui-binary plugin-id\n";
    return 2;
  }
  Module a, b;
  if (!a.open(argv[1], argv[3]) || !b.open(argv[2], argv[3]))
    return 2;
  check(a.infos.size() == b.infos.size(), "same parameter count");
  if (a.infos.size() != b.infos.size())
    return 1;
  for (size_t i = 0; i < a.infos.size(); ++i) {
    const auto &x = a.infos[i], &y = b.infos[i];
    check(x.id == y.id && x.flags == y.flags && x.min_value == y.min_value &&
              x.max_value == y.max_value &&
              x.default_value == y.default_value &&
              std::strcmp(x.name, y.name) == 0 &&
              std::strcmp(x.module, y.module) == 0,
          "unchanged parameter metadata");
  }
  compareParams(a, b);
  for (unsigned scene = 0; scene < 3; ++scene) {
    Events edits;
    for (size_t i = 0; i < a.infos.size(); ++i) {
      const auto &info = a.infos[i];
      if (info.flags & CLAP_PARAM_IS_READONLY)
        continue;
      double value = scene == 0 ? info.default_value
                                : info.min_value +
                                      double((i * 13 + scene * 17) % 97) / 97. *
                                          (info.max_value - info.min_value);
      if (info.flags & CLAP_PARAM_IS_STEPPED)
        value = std::round(value);
      edits.add(info.id, value);
    }
    a.params->flush(a.plugin, &edits.input, nullptr);
    b.params->flush(b.plugin, &edits.input, nullptr);
    compareParams(a, b);
    exchangeState(a, b);
    exchangeState(b, a);
    // Exercise the supported original double-precision processing paths too.
    const std::string id = argv[3];
    const bool supportsDouble =
        id.find("ambi-ray") != std::string::npos ||
        id.find("accelerometer-field") != std::string::npos ||
        id.find("ambi-encoder-medium") != std::string::npos ||
        id.find("ambi-encoder-membrane-kick") != std::string::npos ||
        id.find("low-frequency-synth") != std::string::npos ||
        id.find("processor-lowform") != std::string::npos ||
        id.find("processor-stack") != std::string::npos ||
        id.find("processor-conduit") != std::string::npos ||
        id.find("processor-errant") != std::string::npos ||
        id.find("feedback-shift") != std::string::npos ||
        id.find("processor-fissure") != std::string::npos ||
        id == "org.s3g.s3g-dsp.fault";
    Stream beforeFloatA, beforeFloatB;
    if (supportsDouble) {
      check(a.state->save(a.plugin, &beforeFloatA.out) &&
                b.state->save(b.plugin, &beforeFloatB.out),
            "save initial scene for both sample formats");
    }
    compareAudio<float>(a, b, scene);
    if (supportsDouble) {
      // The float pass automates every parameter, including MIDI receive.
      // Restore the scene so double starts with the same defaults/edits,
      // rather than potentially filtering out all of the test's note events.
      check(a.state->load(a.plugin, &beforeFloatA.in) &&
                b.state->load(b.plugin, &beforeFloatB.in),
            "restore initial scene before double processing");
      compareParams(a, b);
      compareAudio<double>(a, b, scene);
    }
  }
  if (ok)
    std::cout << argv[3]
              << " Cocoa/VSTGUI metadata, state and audio parity passed\n";
  return ok ? 0 : 1;
}
