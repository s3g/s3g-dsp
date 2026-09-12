// Compare environmental encoders in independently built Cocoa and VSTGUI
// binaries. This deliberately does not include their implementation sources or
// duplicate their DSP math.
#include <algorithm>
#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
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

void renderPair(Module &a, Module &b, unsigned scene,
                const std::vector<uint8_t> &surface) {
  constexpr unsigned frames = 64, channels = 64;
  if (!surface.empty()) {
    Stream sa, sb;
    sa.bytes = surface;
    sb.bytes = surface;
    check(a.state->load(a.plugin, &sa.in) && b.state->load(b.plugin, &sb.in),
          "same populated SURF state");
  }
  check(a.plugin->activate(a.plugin, 48000, 1, frames) &&
            b.plugin->activate(b.plugin, 48000, 1, frames),
        "activate");
  // Reset both engines to the identical deterministic scene.
  a.plugin->reset(a.plugin);
  b.plugin->reset(b.plugin);
  check(a.plugin->start_processing(a.plugin) &&
            b.plugin->start_processing(b.plugin),
        "start");
  std::vector<std::vector<float>> ao(channels, std::vector<float>(frames)),
      bo = ao;
  std::vector<float *> ap(channels), bp(channels);
  for (unsigned c = 0; c < channels; ++c) {
    ap[c] = ao[c].data();
    bp[c] = bo[c].data();
  }
  clap_audio_buffer_t ab{}, bb{};
  ab.channel_count = bb.channel_count = channels;
  ab.data32 = ap.data();
  bb.data32 = bp.data();
  double error = 0, energy = 0;
  for (unsigned block = 0; block < 800; ++block) {
    Events events;
    if (block == 100 || block == 400) {
      for (size_t i = 0; i < a.infos.size(); ++i) {
        const auto &info = a.infos[i];
        if (info.flags & CLAP_PARAM_IS_READONLY)
          continue;
        if (info.id == 1)
          continue; // keep the selected preset/scene identity
        const double n = double((i * 7 + block + scene * 11) % 79) / 79.;
        double v = info.min_value + n * (info.max_value - info.min_value);
        if (info.flags & CLAP_PARAM_IS_STEPPED)
          v = std::round(v);
        events.add(info.id, v);
      }
    }
    clap_process_t p{};
    p.frames_count = frames;
    p.audio_outputs_count = 1;
    p.in_events = &events.input;
    p.audio_outputs = &ab;
    check(a.plugin->process(a.plugin, &p) != CLAP_PROCESS_ERROR,
          "Cocoa process");
    p.audio_outputs = &bb;
    check(b.plugin->process(b.plugin, &p) != CLAP_PROCESS_ERROR,
          "VSTGUI process");
    for (unsigned c = 0; c < channels; ++c)
      for (unsigned f = 0; f < frames; ++f) {
        check(std::isfinite(ao[c][f]) && std::isfinite(bo[c][f]),
              "finite audio");
        error = std::max(error, double(std::abs(ao[c][f] - bo[c][f])));
        energy += std::abs(ao[c][f]);
      }
  }
  check(error < 1.e-6, "Cocoa/VSTGUI audio mismatch");
  check(energy > 1.e-7, "non-silent source");
  std::cout << "scene " << scene << " maximum audio error " << error << '\n';
  a.plugin->stop_processing(a.plugin);
  b.plugin->stop_processing(b.plugin);
  a.plugin->deactivate(a.plugin);
  b.plugin->deactivate(b.plugin);
  compareParams(a, b);
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "usage: parity cocoa-binary portable-binary plugin-id "
                 "[populated-surface-state]\n";
    return 2;
  }
  for (unsigned scene = 0; scene < 4; ++scene) {
    Module a, b;
    if (!a.open(argv[1], argv[3]) || !b.open(argv[2], argv[3]))
      return 1;
    check(a.infos.size() == b.infos.size(), "parameter count");
    for (size_t i = 0; i < std::min(a.infos.size(), b.infos.size()); ++i) {
      const auto &x = a.infos[i];
      const auto &y = b.infos[i];
      check(x.id == y.id && x.flags == y.flags && x.min_value == y.min_value &&
                x.max_value == y.max_value &&
                x.default_value == y.default_value &&
                std::strcmp(x.name, y.name) == 0 &&
                std::strcmp(x.module, y.module) == 0,
            "parameter metadata parity");
    }
    check(a.ports->count(a.plugin, true) == 0 &&
              b.ports->count(b.plugin, true) == 0,
          "zero-input instruments");
    clap_audio_port_info_t pa{}, pb{};
    check(a.ports->get(a.plugin, 0, false, &pa) &&
              b.ports->get(b.plugin, 0, false, &pb) && pa.channel_count == 64 &&
              pb.channel_count == 64,
          "64-channel ACN/SN3D");
    Events e;
    e.add(1, scene == 0 ? 0 : scene == 1 ? a.infos[0].max_value : 7);
    a.params->flush(a.plugin, &e.input, nullptr);
    b.params->flush(b.plugin, &e.input, nullptr);
    compareParams(a, b);
    exchangeState(a, b);
    exchangeState(b, a);
    std::vector<uint8_t> surface;
    if (scene == 3 && argc == 5) {
      std::ifstream f(argv[4], std::ios::binary);
      surface.assign(std::istreambuf_iterator<char>(f), {});
      check(!surface.empty(), "populated SURF fixture");
    }
    renderPair(a, b, scene, surface);
  }
  if (ok)
    std::cout << "Independent Cocoa/VSTGUI parity passed: " << argv[3] << '\n';
  return ok ? 0 : 1;
}
