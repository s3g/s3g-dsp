void renderPair(Module &a, Module &b) {
  constexpr unsigned frames = 128, channels = 64;
  clap_audio_port_info_t ai{}, bi{}, ao{}, bo{};
  const auto inputs = a.ports->count(a.plugin, true);
  check(inputs == b.ports->count(b.plugin, true), "input port count parity");
  if (inputs) {
    check(a.ports->get(a.plugin, 0, true, &ai) &&
              b.ports->get(b.plugin, 0, true, &bi) &&
              ai.channel_count == bi.channel_count,
          "input channels parity");
  }
  check(a.ports->get(a.plugin, 0, false, &ao) &&
            b.ports->get(b.plugin, 0, false, &bo) &&
            ao.channel_count == bo.channel_count,
        "output channels parity");
  std::vector<std::vector<float>> input(channels, std::vector<float>(frames)),
      left = input, right = input;
  std::vector<float *> ip(channels), lp(channels), rp(channels);
  for (unsigned c = 0; c < channels; ++c) {
    ip[c] = input[c].data();
    lp[c] = left[c].data();
    rp[c] = right[c].data();
  }
  clap_audio_buffer_t in{}, out{};
  in.channel_count = ai.channel_count;
  in.data32 = ip.data();
  out.channel_count = ao.channel_count;
  check(a.plugin->activate(a.plugin, 48000, 1, frames) &&
            b.plugin->activate(b.plugin, 48000, 1, frames),
        "activate");
  a.plugin->reset(a.plugin);
  b.plugin->reset(b.plugin);
  check(a.plugin->start_processing(a.plugin) &&
            b.plugin->start_processing(b.plugin),
        "start");
  double error = 0, energy = 0;
  for (unsigned block = 0; block < 240; ++block) {
    for (unsigned c = 0; c < channels; ++c)
      for (unsigned f = 0; f < frames; ++f)
        input[c][f] = float(.1 * std::sin((block * frames + f) * .035 + c));
    Events events;
    if (block == 100)
      for (const auto &info : a.infos) {
        if (info.flags & (CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED))
          continue;
        events.add(info.id,
                   info.min_value + .37 * (info.max_value - info.min_value));
      }
    clap_process_t p{};
    p.frames_count = frames;
    p.steady_time = block * frames;
    p.in_events = &events.input;
    p.audio_inputs_count = inputs ? 1 : 0;
    p.audio_inputs = &in;
    p.audio_outputs_count = 1;
    p.audio_outputs = &out;
    out.data32 = lp.data();
    check(a.plugin->process(a.plugin, &p) != CLAP_PROCESS_ERROR,
          "Cocoa processing");
    out.data32 = rp.data();
    check(b.plugin->process(b.plugin, &p) != CLAP_PROCESS_ERROR,
          "portable processing");
    for (unsigned c = 0; c < ao.channel_count; ++c)
      for (unsigned f = 0; f < frames; ++f) {
        check(std::isfinite(left[c][f]) && std::isfinite(right[c][f]),
              "finite audio");
        error = std::max(error, double(std::abs(left[c][f] - right[c][f])));
        energy += std::abs(left[c][f]);
      }
  }
  check(error < 1.e-6, "Cocoa/VSTGUI audio parity");
  std::cout << "Maximum audio error: " << error << "; energy: " << energy
            << '\n';
  a.plugin->stop_processing(a.plugin);
  b.plugin->stop_processing(b.plugin);
  a.plugin->deactivate(a.plugin);
  b.plugin->deactivate(b.plugin);
  compareParams(a, b);
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: parity cocoa-binary portable-binary plugin-id\n";
    return 2;
  }
  Module a, b;
  if (!a.open(argv[1], argv[3]) || !b.open(argv[2], argv[3]))
    return 2;
  check(a.infos.size() == b.infos.size(), "parameter count parity");
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
  compareParams(a, b);
  exchangeState(a, b);
  exchangeState(b, a);
  renderPair(a, b);
  if (ok)
    std::cout << argv[3]
              << " Cocoa/VSTGUI metadata/state/audio parity passed\n";
  return ok ? 0 : 1;
}
