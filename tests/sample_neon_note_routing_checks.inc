void testKeyboardRangeFeedback() {
    namespace nm = s3g::controller::neon_midi;
    auto instance=historyFixture();auto& p=*instance;
    historySource(p,0,0,historyAudio());
    p.channelTargets[1].store(1);p.rootNotes[0].store(72);
    setParam(p,kNeonActiveParamId,1);
    selectSurfaceBank(p,1);p.selectedSlot.store(9);
    selectSurface(p,NeonMode::HotCue,1);selectSurfaceBank(p,2);saveSurface(p);
    struct Input {
        clap_event_midi_sysex_t event {};
        clap_input_events_t list {this,[](const clap_input_events_t*)->uint32_t{return 1;},
            [](const clap_input_events_t* in,uint32_t n)->const clap_event_header_t* {
                return n ? nullptr : &static_cast<const Input*>(in->ctx)->event.header;
            }};
    } in;
    in.event.header={sizeof(in.event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
    auto send=[&](nm::BridgeMessage message) {
        auto packet=nm::encodeAddressedBridge(message);
        auto legacy=nm::encodeBridge(message);
        in.event.buffer=message.addressed ? packet.data() : legacy.data();
        in.event.size=message.addressed ? packet.size() : legacy.size();
        return collectEvents(p,&in.list,nullptr,64);
    };
    const auto original=neonFeedbackFrame(p,p.surfaces[0],0);
    for(unsigned unit : {1u,0u}) for(unsigned bank=0;bank<4;++bank) {
        check(send({nm::BridgeKind::KeyboardRange,uint8_t(bank),36,1,{},0,true,uint8_t(unit),int32_t(1234+unit)})==0,
            "range update emits no audition or stop events");
        check(p.surfaceUnit==0&&p.visibleUnit.load()==0&&p.selectedSlot.load()==9
            &&p.visibleBank.load()==2&&p.cellBank.load()==1&&p.stackBank.load()==2
            &&p.visibleMode.load()==uint8_t(NeonMode::HotCue)&&p.visibleLayer.load()==1,
            "keyboard range preserves focused and nonfocused editor banks, cell, page and secondary layer");
        const auto feedback=neonFeedbackFrame(p,p.surfaces[unit],unit);
        check(feedback.bank==bank&&feedback.mode==NeonMode::Sampler
            &&feedback.layer==s3g::controller::reloop_neon::Layer::First
            &&p.surfaceDestinations[unit]==int32_t(1234+unit),"keyboard feedback targets its own bank and primary SAMPLE page");
        if(bank==3) check(feedback.pads[0].surface==104&&feedback.pads[1].surface==64,
            "range feedback lights pitches from the chosen range, not editor bank");
        if(unit==1) {
            const auto other=neonFeedbackFrame(p,p.surfaces[0],0);
            check(other.bank==original.bank&&other.mode==original.mode&&other.layer==original.layer,
                "keyboard range cannot alter other controller feedback");
        }
    }
    // Legacy host-MIDI ownership and OWNER off still retain isolated feedback.
    check(send({nm::BridgeKind::KeyboardRange,1,36,1})==0&&p.visibleBank.load()==2
        &&p.visibleMode.load()==uint8_t(NeonMode::HotCue),"single legacy keyboard leaves software page unchanged");
    setParam(p,kNeonActiveParamId,0);
    send({nm::BridgeKind::KeyboardRange,3,36,1,{},0,true,1,9876});
    check(p.surfaceDestinations[1]==9876&&p.utilityRangeActive[1]&&p.utilityRanges[1]==3
        &&p.visibleBank.load()==2,"OWNER off caches keyboard assignment/range without navigating");
    setParam(p,kNeonActiveParamId,1);
    // Return unit 0 to Pad Cells; unit 1 must remain an independent keyboard.
    send({nm::BridgeKind::Sync,3,36,0,{0,0,0},0,true,0,1234});
    send({nm::BridgeKind::KeyboardSetup,3,36,0,{0,0,48},0,true,0,1234});
    check(!p.utilityRangeActive[0]&&p.visibleBank.load()==3&&p.cellBank.load()==3,
        "switching back to Pad Cells restores its ordinary bank from synchronization");
    send({nm::BridgeKind::Control,1,36,0,{0x94,6,127},0,true,0,1234});
    check(!p.utilityRangeActive[0]&&p.utilityRangeActive[1]
        &&p.visibleMode.load()==uint8_t(NeonMode::Slicer),"Pad Cells controller still selects editing pages independently");
    const auto keyboard=neonFeedbackFrame(p,p.surfaces[1],1);
    check(keyboard.bank==3&&keyboard.mode==NeonMode::Sampler,"other unit's CHOP command cannot replace keyboard LED page/range");
    send({nm::BridgeKind::Control,3,36,1,{0x93,7,127},0,true,1,9876});
    check(!p.utilityRangeActive[1]&&p.visibleMode.load()==uint8_t(NeonMode::HotCue),
        "deliberate keyboard-unit STACK press leaves isolation for editing");
    send({nm::BridgeKind::Disconnect,3,36,1,{},0,true,1,9876});
    check(!p.utilityRangeActive[1]&&!p.surfaceConnected[1],"disconnect retires keyboard feedback context");
}

void testPinnedNoteRouting() {
    testKeyboardRangeFeedback();
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
    auto keys=s3g::controller::neon_midi::keyboardNotes(1,57,1);
    keys[1]=255;
    const auto packet=s3g::controller::neon_midi::encodeKeyboardMap(0,2,keys);
    struct MapInput {
        clap_event_midi_sysex_t event {};
        clap_input_events_t list {this,[](const clap_input_events_t*)->uint32_t{return 1;},
            [](const clap_input_events_t* input,uint32_t index)->const clap_event_header_t* {
                return index ? nullptr : &static_cast<const MapInput*>(input->ctx)->event.header;
            }};
    } map;
    map.event.header={sizeof(map.event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
    map.event.buffer=packet.data();map.event.size=packet.size();
    p.channelTargets[2].store(1);selectSurfaceBank(p,0);saveSurface(p);
    n=collectEvents(p,&map.list,nullptr,64);
    check(n==0&&p.utilityKeyboardNotes[0]==keys&&p.utilityKeyboardChannels[0]==2,
        "Utility keyboard map updates surface feedback without triggering or replacing the pad-note map");
    const auto mappedLamps=neonFeedbackFrame(p,p.surfaces[0]);
    check(mappedLamps.pads[0].surface==104&&mappedLamps.pads[1].surface==0&&mappedLamps.pads[2].surface==64,
        "scaled/manual keyboard feedback follows mapped root and OFF keys");
}
