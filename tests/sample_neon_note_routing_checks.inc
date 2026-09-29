void testPinnedNoteRouting() {
    auto instance = historyFixture(); auto& p = *instance;
    historySource(p,0,0,historyAudio()); historySource(p,9,0,historyAudio());
    p.channelTargets[1].store(1); p.channelTargets[2].store(10);
    p.noteVoiceModes[0].store(2); p.noteVoiceLimits[0].store(4); p.rootNotes[0].store(57);
    p.selectedSlot.store(9);
    struct Input {
        clap_event_note_t note {};
        clap_input_events_t list {this,[](const clap_input_events_t*)->uint32_t { return 1; },
            [](const clap_input_events_t* in,uint32_t n)->const clap_event_header_t* {
                return n ? nullptr : &static_cast<const Input*>(in->ctx)->note.header;
            }};
    } in;
    in.note.header = {sizeof(in.note),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_NOTE_ON,0};
    in.note.port_index = 0; in.note.channel = 1; in.note.key = 69; in.note.note_id = 12; in.note.velocity = .5;
    unsigned n = collectEvents(p,&in.list,nullptr,64);
    check(n==1 && p.blockEvents[0].slot==0 && p.blockEvents[0].key==69 && p.blockEvents[0].value==.5f,
        "pinned channel routes chromatic key independently of selected edit pad");
    const auto identity = p.blockEvents[0].noteId;
    p.channelTargets[1].store(10); setParam(p,kMidiReceiveParamId,3);
    in.note.header.type = CLAP_EVENT_NOTE_OFF; in.note.channel = -1; in.note.key = -1;
    n = collectEvents(p,&in.list,nullptr,64);
    check(n==1 && p.blockEvents[0].slot==0 && p.blockEvents[0].noteId==identity,
        "CLAP wildcard note-off survives channel filter and target changes");
    setParam(p,kMidiReceiveParamId,0);
    check(copyCell(p,0) && pasteCell(p,9),"voice controls copy with pad cell");
    check(p.noteVoiceModes[9].load()==2 && p.noteVoiceLimits[9].load()==4 && p.rootNotes[9].load()==57,
        "cell paste retains mode, limit and root");
    historyStep(p);
    check(p.noteVoiceModes[9].load()==0 && p.noteVoiceLimits[9].load()==8 && p.rootNotes[9].load()==60,
        "Undo restores destination voice controls");
    collectEvents(p,nullptr,nullptr,64); // Drain the prior paste/Undo's explicit pad-stop commands.
    p.keyboardTargets[0].store(0); p.keyboardFirstNotes[0].store(48);
    setParam(p,kNeonActiveParamId,1);
    p.selectedSlot.store(9); selectSurface(p,NeonMode::Sampler,0); selectSurfaceBank(p,1);
    struct Raw {
        clap_event_midi_t event {};
        clap_input_events_t list {this,[](const clap_input_events_t*)->uint32_t{return 1;},
            [](const clap_input_events_t* in,uint32_t n)->const clap_event_header_t* {
                return n ? nullptr : &static_cast<const Raw*>(in->ctx)->event.header;
            }};
    } raw;
    raw.event.header = {sizeof(raw.event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};
    raw.event.data[0]=0x98;raw.event.data[1]=1;raw.event.data[2]=111;
    n=collectEvents(p,&raw.list,nullptr,64);
    check(n==1 && p.blockEvents[0].slot==0 && p.blockEvents[0].key==57 && p.selectedSlot.load()==9,
        "direct Keyboard B2 plays pinned A1 at first+9 without changing edit focus");
    p.keyboardTargets[0].store(9);p.keyboardFirstNotes[0].store(60);selectSurfaceBank(p,3);
    raw.event.data[0]=0x88;raw.event.data[2]=0;n=collectEvents(p,&raw.list,nullptr,64);
    check(n==1 && p.blockEvents[0].slot==0 && p.blockEvents[0].key==57 && p.blockEvents[0].kind==SampleNeonEventKind::Release,
        "direct Keyboard release stays with original cell and key after bank/target changes");
    p.keyboardTargets[0].store(0);p.keyboardFirstNotes[0].store(48);selectSurfaceBank(p,1);saveSurface(p);
    const auto lamps=neonFeedbackFrame(p,p.surfaces[0]);
    check(lamps.pads[1].surface==104 && lamps.pads[0].surface==64,
        "Keyboard LEDs mark root and loaded pitched keys while preserving bank/page");
    in.note.header.type=CLAP_EVENT_NOTE_ON;in.note.channel=1;in.note.key=69;in.note.note_id=33;
    n=collectEvents(p,&in.list,nullptr,64);
    check(n==1 && p.blockEvents[0].slot==9 && p.blockEvents[0].kind==SampleNeonEventKind::Trigger,
        "panic fixture starts a CLAP note at the changed channel target");
    const auto panicIdentity=p.blockEvents[0].noteId;
    raw.event.data[0]=0xb1;raw.event.data[1]=123;raw.event.data[2]=0;
    n=collectEvents(p,&raw.list,nullptr,64);
    check(n==33 && p.blockEvents[0].slot==9 && p.blockEvents[0].noteId==panicIdentity
        && p.blockEvents[0].kind==SampleNeonEventKind::Choke,
        "MIDI channel panic also stops notes delivered in CLAP dialect");
    for(unsigned slot=0;slot<32;++slot)
        check(p.blockEvents[slot+1].kind==SampleNeonEventKind::StackPosition
            && p.blockEvents[slot+1].slot==slot && p.blockEvents[slot+1].value==-1,
            "panic retains the existing per-pad stack-navigation reset");
}
