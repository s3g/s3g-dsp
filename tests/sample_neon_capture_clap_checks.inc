if (variant == 4 && ok) {
    StateBuffer live; live.bytes = modernPlayback.bytes; live.bytes[4] = 26;
    const double target = 1; // Empty A2, beside the existing 32-layer A1.
    std::memcpy(live.bytes.data() + expectedStateBytes - kCaptureStateBytes + 4 * sizeof(double), &target, sizeof(target));
    const std::array<uint8_t, 6> setup {{1, 6, 0, 1, 255, 0}}; // track input / 3OA / pins 1-16 / stack / next empty / monitor off
    live.bytes.insert(live.bytes.end(), setup.begin(), setup.end());
    ok = expect(stackState->load(stacked, &live.input), "v26 live-input setup load") && ok;
    StateBuffer again;
    ok = expect(stackState->save(stacked, &again.output) && again.bytes == live.bytes, "v26 byte-identical setup recall") && ok;
    for (unsigned field = 0; field < setup.size(); ++field) {
        StateBuffer bad; bad.bytes = live.bytes; bad.bytes[bad.bytes.size() - 6 + field] = field == 4 ? 32 : 254;
        ok = expect(!stackState->load(stacked, &bad.input), "invalid recorder setup rejected") && ok;
    }
    std::array<std::array<float, 64>, 32> inputAudio {};
    std::array<float*, 32> inputPins {};
    for (unsigned ch = 0; ch < 32; ++ch) { inputAudio[ch].fill(.01f * (ch + 1)); inputPins[ch] = inputAudio[ch].data(); }
    clap_audio_buffer_t trackInput {}; trackInput.data32 = inputPins.data(); trackInput.channel_count = 32;
    MidiInput record; record.list.size = [](const clap_input_events_t*) -> uint32_t { return 1; };
    int recordPage=-1;
    const auto recordMessage = [&](uint8_t status, uint8_t note, uint8_t velocity = 127) {
        record.performanceGesture(status,note,velocity,recordPage);
        ok = processChannels(stacked, record, feedback, output, &trackInput) && ok;
    };
    stacked->reset(stacked); ok = expect(stacked->start_processing(stacked), "live input processing starts") && ok;
    recordMessage(0x97, 0x18); // Primary Record starts on press.
    recordMessage(0x87, 0x18, 0); // Release must not stop recording.
    recordMessage(0x97, 0x39); // Shift + primary pad 2 = TAKE / NEXT.
    stacked->on_main_thread(stacked);
    recordMessage(0x87, 0x39, 0); // Take release must not stop or split again.
    recordMessage(0x97, 0x18); // Same unshifted Record press stops.
    stacked->on_main_thread(stacked);
    StateBuffer takes; ok = expect(stackState->save(stacked, &takes.output), "live takes saved") && ok;
    const size_t a2 = expectedStateBytes + 1 + 12 + 32 * 309;
    ok = expect(takes.bytes.size() > a2 && takes.bytes[a2] == 2, "hardware record/take created exactly two layers") && ok;
    stacked->reset(stacked);
    recordMessage(0x97, 0x01); // Play A2 through ordinary pad mapping.
    ok = expect(maximumMagnitude(output[0]) > .001f, "recorded input layer plays") && ok;
    for (unsigned ch = 1; ch < 16; ++ch) for (unsigned frame = 0; frame < 64; ++frame)
        ok = expect(std::abs(output[ch][frame] - output[0][frame] * (ch + 1)) < 1.e-5f, "recorded ACN field stays channel coherent") && ok;
    stacked->stop_processing(stacked);
}
