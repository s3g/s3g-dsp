void testNotePolyphony() {
    using namespace s3g::sample;
    using namespace s3g::controller::neon_midi;
    auto engine = std::make_unique<SampleNeonPolyEngine>();
    auto owned = std::make_shared<SampleAsset>(rampAsset(48000));
    auto& source = *owned;
    for (unsigned n = 0; n < source.frameCount(); ++n)
        source.channels[0][n] = source.channels[1][n] = .3f * std::sin(n * .03);
    const auto map = analyzeWavesets(owned);
    check(engine->prepare(48000,64),"note voice pool prepares");
    engine->setPreparedAsset(0,&source); engine->setPreparedWavesets(0,map.get());
    SampleNeonSettings settings;
    auto& pad = settings.slots[0]; pad.noteVoiceMode = 2; pad.noteVoiceLimit = 4;
    pad.triggerMode = TriggerMode::Gate; pad.repeat = true;
    pad.techniqueAttackSeconds = .001; pad.techniqueReleaseSeconds = .001;
    pad.family[NeonFamily::CutRelease] = .001;
    OutputBlock output;
    SampleNeonEvent a; a.noteId = 1; a.key = 60;
    auto b = a; b.noteId = 2; b.key = 72; b.frameOffset = 16;
    for (unsigned method = 0; method <= 8; ++method) {
        engine->reset(); pad.playback = static_cast<SampleNeonPlayback>(method);
        const std::array<SampleNeonEvent,2> on {{a,b}};
        engine->render(settings,on.data(),on.size(),output.pointers.data(),32,64);
        check(engine->activeNoteCount(0) == 2,"every method allocates independent pitched gestures");
        for (unsigned block = 0; block < 5; ++block) engine->render(settings,nullptr,0,output.pointers.data(),32,64);
        bool finite = true, audible = false;
        for (const auto& channel : output.samples) for (float x : channel) { finite &= std::isfinite(x); audible |= std::abs(x) > 1e-6; }
        check(finite && audible,"polyphonic method renders finite audible output");
        auto off = a; off.kind = SampleNeonEventKind::Release;
        engine->render(settings,&off,1,output.pointers.data(),32,64);
        for (unsigned block = 0; block < 200; ++block) engine->render(settings,nullptr,0,output.pointers.data(),32,64);
        check(engine->activeNoteCount(0) == 1,("note-off releases only its gesture, method " + std::to_string(method)).c_str());
        off = b; off.frameOffset = 0; off.kind = SampleNeonEventKind::Choke;
        engine->render(settings,&off,1,output.pointers.data(),32,64);
        check(engine->activeNoteCount(0) == 0,("note-id choke leaves no orphan gesture, method " + std::to_string(method)).c_str());
    }
    // A free-running stack path starts with each note, not the latest trigger.
    NeonStack stack; stack.count = 2; stack.layers[0] = {&source,0,1}; stack.layers[1] = {&source,0,1};
    pad.stack = &stack; pad.playback = SampleNeonPlayback::Grains; pad.sourceMode = NeonSourceMode::Scan;
    engine->reset(); engine->render(settings,&a,1,output.pointers.data(),32,64);
    for (unsigned n = 0; n < 20; ++n) engine->render(settings,nullptr,0,output.pointers.data(),32,64);
    engine->render(settings,&b,1,output.pointers.data(),32,64);
    check(engine->activeNoteCount(0) == 2 && engine->stackPathPhase(0,settings) < .01f,"new stack path does not restart an older gesture");
    pad.stack = nullptr; pad.playback = SampleNeonPlayback::Sample;
    pad.noteVoiceMode = 3; engine->reset();
    engine->render(settings,&a,1,output.pointers.data(),32,64);
    auto legato = b; legato.frameOffset = 0;
    engine->render(settings,&legato,1,output.pointers.data(),32,64);
    check(engine->activeNoteCount(0) == 1 && engine->voiceCursors(0)[0].key == 72,"legato retunes one existing gesture");
    legato.kind = SampleNeonEventKind::Release;
    engine->render(settings,&legato,1,output.pointers.data(),32,64);
    check(engine->activeNoteCount(0) == 1 && engine->voiceCursors(0)[0].key == 60,"legato returns to last held pitch");
    pad.noteVoiceMode = 2; pad.noteVoiceLimit = 2; engine->reset();
    std::array<SampleNeonEvent,4> chord {{a,a,a,a}};
    for (unsigned n = 0; n < chord.size(); ++n) { chord[n].noteId = n+1; chord[n].key = 60+n; }
    engine->render(settings,chord.data(),chord.size(),output.pointers.data(),32,64);
    check(engine->activeNoteCount(0) == 2,"same-frame overflow steals to the per-pad limit");
    check(engine->voiceCursorCount(0) == 2,"stolen queued note-ons cannot leak extra voices");
    pad.stack = &stack; pad.playback = SampleNeonPlayback::Cutups;
    pad.family[NeonFamily::CutPolyPath] = 1;
    const std::array<SampleNeonEvent,2> cutChord {{a,b}};
    engine->reset();engine->render(settings,cutChord.data(),2,output.pointers.data(),32,64);
    unsigned layers = 0;
    for (unsigned n = 0; n < engine->voiceCursorCount(0); ++n) layers |= 1u << engine->voiceCursors(0)[n].layer;
    check((layers & 3u) == 3u,"Cutups step-offset relationship spans independent outer note voices");
    settings.outputLayout = SampleNeonOutputLayout::Quad;
    pad.family[NeonFamily::RoutingMode] = 1; pad.family[NeonFamily::RoutingWidth] = 0;
    for (auto playback : {SampleNeonPlayback::Lanes,SampleNeonPlayback::Cutups}) {
        pad.playback = playback; engine->reset();engine->render(settings,cutChord.data(),2,output.pointers.data(),32,64);
        bool first = false,second = false;
        for (float x : output.samples[0]) first |= std::abs(x)>1e-7;
        for (float x : output.samples[1]) second |= std::abs(x)>1e-7;
        check(first && second,"polyphonic distribution shares the pad allocator instead of restarting at output one");
    }
    // Router uses channel to address pads; incoming key changes pitch only.
    NoteRouter router; std::vector<SampleNeonEvent> routed;
    const auto sink = [&](auto e) { routed.push_back(e); return true; };
    const auto notes = sequentialNotes();
    router.on(0,1,72,10,false,1,notes, .5f,sink);
    router.on(0,2,72,11,false,11,notes,.75f,sink);
    check(routed.size() == 2 && routed[0].slot == 0 && routed[1].slot == 10 && routed[0].key == 72,"channel-to-pad chromatic routing");
    router.off(3,-1,-1,10,false,false,sink);
    check(routed.size() == 3 && routed.back().slot == 0 && routed.back().noteId == routed[0].noteId,"CLAP wildcard channel/key release matches note identity");
    router.off(4,2,72,-1,false,true,sink);
    check(routed.size() == 4 && routed.back().slot == 10,"latched release survives destination edits");
    router.on(5,0,notes[7],-1,true,0,notes,1,sink);
    check(routed.back().slot == 7 && routed.back().key == 255,"ordinary pad map never transposes by address");
    const auto size = routed.size(); router.on(6,0,60,-1,true,33,notes,1,sink);
    check(routed.size() == size,"disabled MIDI channel creates no voice");
}
