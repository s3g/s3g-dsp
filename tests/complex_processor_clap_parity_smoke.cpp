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
    // Imprint deliberately accepts old files without the optional camera tail.
    // For an empty response, half of the current state is exactly that valid
    // legacy header, not a truncated required payload.
    if (std::strstr(b.plugin->desc->id, "ambi-imprint") &&
        length == bytes.size() / 2)
      continue;
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

#include "complex_processor_clap_parity_cases.inc"
