if (variant == 4 && ok) {
    StateBuffer poly; poly.bytes = modernPlayback.bytes; poly.bytes[4] = 27;
    // The older routing fixture is DC. Give all embedded layers a linked sine
    // field so Spectral's DC rejection is not mistaken for missing polyphony.
    std::size_t pcm = expectedStateBytes + 1u + 32u*12u + 32u*309u;
    for (unsigned layer = 0; layer < 1025; ++layer) {
        uint32_t channels = 0, frames = 0;
        std::memcpy(&channels,poly.bytes.data()+pcm,4); std::memcpy(&frames,poly.bytes.data()+pcm+4,4); pcm += 16;
        for (unsigned ch = 0; ch < channels; ++ch) for (unsigned f = 0; f < frames; ++f) {
            const float value = static_cast<float>(.01 * (ch+1) * std::sin(f*.07 + layer*.2));
            std::memcpy(poly.bytes.data()+pcm,&value,4); pcm += 4;
        }
    }
    const std::array<uint8_t,6> recorder {{0,1,0,0,255,0}};
    poly.bytes.insert(poly.bytes.end(),recorder.begin(),recorder.end());
    std::array<uint8_t,116> setup {};
    for (unsigned p = 0; p < 32; ++p) { setup[p*3+1] = 4; setup[p*3+2] = 60; }
    setup[0] = 2; setup[97] = 1; // CH2 chromatic A1, other channels retain pad map.
    setup[112] = 0; setup[113] = 48; setup[114] = 255; setup[115] = 48;
    poly.bytes.insert(poly.bytes.end(),setup.begin(),setup.end());
    setStateParameter(poly,6,2); // Explicit CH2 musical input alongside the private control bridge.
    poly.bytes[kPerformanceStateOffset] |= 1; // Loop the short fixture in ordinary Sample mode.
    ok = expect(stackState->load(stacked,&poly.input),"v27 voice/channel/keyboard state loads") && ok;
    StateBuffer roundtrip;
    ok = expect(stackState->save(stacked,&roundtrip.output) && roundtrip.bytes == poly.bytes,"v27 byte-identical voice state recall") && ok;
    for (unsigned field : {0u,1u,2u,97u,112u,113u}) {
        StateBuffer invalid; invalid.bytes = poly.bytes; invalid.bytes[invalid.bytes.size()-116+field] = 254;
        ok = expect(!stackState->load(stacked,&invalid.input),"invalid v27 voice/routing field rejected") && ok;
    }
    constexpr std::array<uint8_t,9> methods {{0,1,4,8,16,32,64,128,2}};
    for (unsigned method = 0; method < methods.size(); ++method) {
        if (method == 5) continue; // This fixture is 16ch ACN; Wavesets/Oscillator intentionally reject it (stereo core coverage).
        StateBuffer exercise; exercise.bytes = poly.bytes;
        exercise.bytes[textureOffset] = methods[method];
        if (method == 5) setStateParameter(exercise,7+16,0); // Oscillator is discrete-only.
        ok = expect(stackState->load(stacked,&exercise.input),"polyphonic playback state loads") && ok;
        stacked->reset(stacked); ok = expect(stacked->start_processing(stacked),"polyphonic playback starts") && ok;
        MidiInput music;
        music.list.size = [](const clap_input_events_t*) -> uint32_t { return 2; };
        for (unsigned n = 0; n < 2; ++n) {
            music.events[n].header.time = n*16;
            music.events[n].data[0] = 0x91; music.events[n].data[1] = n ? 72 : 60; music.events[n].data[2] = n ? 80 : 100;
        }
        ok = processChannels(stacked,music,feedback,output) && ok;
        music.list.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
        float peak = 0;
        for (unsigned n = 0; n < 100; ++n) {
            ok = processChannels(stacked,music,feedback,output) && ok;
            peak = std::max(peak,maximumMagnitude(output[0]));
            for (const auto& channel : output) for (float value : channel) ok = expect(std::isfinite(value),"polyphonic output finite") && ok;
        }
        ok = expect(peak > 1.e-5f,("chromatic chord audible, method " + std::to_string(method)).c_str()) && ok;
        music.list.size = [](const clap_input_events_t*) -> uint32_t { return 1; };
        music.events[0].header.time = 0; music.events[0].data[0] = 0x81; music.events[0].data[1] = 60; music.events[0].data[2] = 0;
        ok = processChannels(stacked,music,feedback,output) && ok;
        music.list.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
        peak = 0;
        for (unsigned n = 0; n < 100; ++n) { ok = processChannels(stacked,music,feedback,output) && ok; peak = std::max(peak,maximumMagnitude(output[0])); }
        ok = expect(peak > 1.e-5f,("first release retains sibling, method " + std::to_string(method)).c_str()) && ok;
        music.list.size = [](const clap_input_events_t*) -> uint32_t { return 1; };
        music.events[0].data[1] = 72; ok = processChannels(stacked,music,feedback,output) && ok;
        music.list.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
        for (unsigned n = 0; n < 400; ++n) ok = processChannels(stacked,music,feedback,output) && ok;
        ok = expect(maximumMagnitude(output[0]) < 1.e-7f,"final note-off leaves no held chromatic gesture") && ok;
        stacked->stop_processing(stacked);
    }
}
