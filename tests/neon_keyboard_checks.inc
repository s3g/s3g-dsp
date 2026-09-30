void testKeyboardRangeIsolation(const char* path, const char* trackerPath) {
    namespace neon = s3g::controller::reloop_neon;
    auto has = [](const List& list, nm::BridgeKind kind, unsigned unit, unsigned bank) {
        bool found = false;
        for (unsigned i=0;i<list.count;++i) {
            const auto& e=list.events[i]; nm::BridgeMessage b;
            if(e.type!=CLAP_EVENT_MIDI_SYSEX || !nm::decodeBridge(e.sysex.buffer,e.sysex.size,b)) continue;
            if(b.unit==unit) {
                found |= b.kind==kind && b.bank==bank;
                if(kind==nm::BridgeKind::KeyboardRange)
                    check(b.kind!=nm::BridgeKind::Control && b.kind!=nm::BridgeKind::Sync && b.kind!=nm::BridgeKind::SelectCell,
                        "keyboard range never sends a navigation envelope");
            }
        }
        return found;
    };
    for(unsigned inputMode : {0u,1u}) for(unsigned unit=0;unit<(inputMode ? 2u : 1u);++unit) {
        Module p(path);p.set(6,inputMode);p.set(unit ? 11 : 9,1);p.set(unit ? 7 : 1,0);p.activate();
        List in,out; p.run(in,out);
        check(has(out,nm::BridgeKind::KeyboardRange,unit,0),"initial keyboard synchronization is feedback-only");
        in.midi(0,0x97,0,73,unit);out.clear();p.run(in,out);
        check(out.notes().size()==1 && out.notes()[0].data[1]==48,"keyboard starts held note in original range");
        for(unsigned bank=0;bank<4;++bank) {
            in.clear();out.clear();
            const auto stale=neon::modeLedMessage(bank,neon::Mode::HotCue,neon::Layer::First);
            in.midi(0,0x93+bank,1,127,unit);
            in.midi(0,stale.status,stale.data1,127,unit);in.midi(0,stale.status,stale.data1,0,unit);
            in.midi(1,0x83+bank,1,0,unit);p.run(in,out);
            check(has(out,nm::BridgeKind::KeyboardRange,unit,bank)&&out.notes().empty(),
                "bank press/release and stale hardware mode recall only change keyboard range");
            check(p.value(unit ? 7 : 1)==bank,"range retained in existing per-unit bank parameter");
            in.clear();out.clear();p.run(in,out);
            if(bank) check(has(out,nm::BridgeKind::KeyboardRange,unit,bank),"following background sync cannot navigate editor");
        }
        in.clear();out.clear();in.midi(0,0xa7,0,51,unit);in.midi(1,0x87,0,0,unit);
        in.midi(2,0x97,1,89,unit);in.midi(3,0x87,1,0,unit);p.run(in,out);
        auto notes=out.notes();
        check(notes.size()==4&&notes[0].data[1]==48&&notes[1].data[1]==48&&notes[2].data[1]==73&&notes[3].data[1]==73,
            "range changes preserve held pressure/release pitch and affect only new notes");
        p.set(unit ? 7 : 1,1);in.clear();out.clear();p.run(in,out);
        check(has(out,nm::BridgeKind::KeyboardRange,unit,1),"GUI or automated range changes are isolated too");
        in.midi(0,0x97,0,80,unit);out.clear();p.run(in,out);
        in.clear();out.clear();out.limit=0;in.midi(0,0x96,1,127,unit);p.run(in,out);
        in.clear();out.clear();p.run(in,out);
        check(has(out,nm::BridgeKind::KeyboardRange,unit,3)&&out.notes().empty(),"rejected LED range retries without releasing held music");
        in.midi(0,0x87,0,0,unit);out.clear();p.run(in,out);
        check(out.notes().size()==1&&out.notes()[0].data[1]==56,"held note survives range feedback backpressure");
        const auto edit=neon::modeLedMessage(0,neon::Mode::HotCue,neon::Layer::First);
        in.clear();out.clear();in.midi(0,edit.status,edit.data1,127,unit);p.run(in,out);
        check(has(out,nm::BridgeKind::Control,unit,3),"deliberate editing page press remains a control");
        const auto play=neon::modeLedMessage(0,neon::Mode::Sampler,neon::Layer::First);
        in.clear();out.clear();in.midi(0,play.status,play.data1,127,unit);p.run(in,out);
        bool restored=false;
        for(unsigned i=0;i<out.count;++i) {const auto& e=out.events[i];nm::BridgeMessage b;
            if(e.type==CLAP_EVENT_MIDI_SYSEX&&nm::decodeBridge(e.sysex.buffer,e.sysex.size,b))
                restored|=b.kind==nm::BridgeKind::KeyboardRange&&b.unit==unit;}
        check(restored,"explicit return to SAMPLE immediately restores isolated feedback");
        if(trackerPath) {
            Module tracker(trackerPath);tracker.activate();
            in.clear();out.clear();in.midi(0,0x94,1,127,unit);p.run(in,out);
            List through;tracker.run(out,through);
            check(has(through,nm::BridgeKind::KeyboardRange,unit,1),"actual Tracker thru preserves range-only messages");
        }
        p.set(unit ? 11 : 9,0);in.clear();out.clear();p.run(in,out);
        check(has(out,nm::BridgeKind::Sync,unit,p.value(unit ? 7 : 1)),"leaving Keyboard restores normal navigation synchronization");
    }
    nm::BridgeMessage range {nm::BridgeKind::KeyboardRange,3,36,1,{},0,true,1,2116083961};
    auto packet=nm::encodeAddressedBridge(range);nm::BridgeMessage decoded;
    check(nm::decodeBridge(packet.data(),packet.size(),decoded)&&decoded.destination==range.destination
        &&decoded.kind==range.kind&&decoded.bank==3,"range wire packet preserves paired hardware destination");
    packet[12]=1;
    check(!nm::decodeBridge(packet.data(),packet.size(),decoded),"range envelope rejects unexpected MIDI payload");
}

void testKeyboardLayouts(const char* path, const char* trackerPath, const char* neonPath) {
    for(unsigned scale=0;scale<s3g::kMusicalScaleCount;++scale) for(unsigned first : {0u,48u,96u}) {
        const auto notes=nm::keyboardNotes(1,first,scale);
        const auto& definition=s3g::musicalScaleDefinition(scale);
        for(unsigned n=0;n<32;++n) {
            const auto expected=first+12*(n/definition.size)+definition.semitones[n%definition.size];
            check(notes[n]==(expected<128 ? expected : 255),"all shared scales use consecutive degrees across four banks without wrapping");
        }
        auto packet=nm::encodeKeyboardMap(1,15,notes);
        uint8_t unit=9,channel=9; nm::PadNotes restored;
        check(nm::decodeKeyboardMap(packet.data(),packet.size(),unit,channel,restored)
            && unit==1 && channel==15 && restored==notes,"7-bit-safe resolved keyboard map roundtrip");
        packet[45]=16;
        check(!nm::decodeKeyboardMap(packet.data(),packet.size(),unit,channel,restored),"invalid OFF-mask high bits rejected");
    }
    nm::PadNotes manual=nm::sequentialNotes(48); manual[1]=manual[0]; manual[2]=255;
    check(nm::validNotes(manual,false,true) && !nm::validNotes(manual),"manual keyboard notes permit duplicate pitches and OFF without relaxing pad-map validation");
    std::string list;
    for(unsigned i=0;i<32;++i) list+=std::to_string(manual[i]==255 ? -1 : int(manual[i]))+", ";
    nm::PadNotes parsed;
    check(nm::parseNotes(list,parsed,false,true)&&parsed==manual&&!nm::parseNotes(list,parsed),"keyboard lists accept duplicates/OFF only in keyboard editor");

    Module p(path); p.set(4,0);p.set(9,1);p.set(14,1);p.set(15,1);p.activate();
    List in,out;
    for(unsigned bank=0;bank<4;++bank) {
        p.set(1,bank);in.clear();out.clear();
        for(unsigned pad=0;pad<8;++pad) { in.midi(pad*4,0x97,pad,70);in.midi(pad*4+1,0x87,pad,0); }
        p.run(in,out);const auto notes=out.notes();const auto expected=nm::keyboardNotes(1,48,1);
        check(notes.size()==16,"scale banks emit eight note pairs");
        for(unsigned pad=0;pad<8;++pad) check(notes[pad*2].data[1]==expected[bank*8+pad]
            && notes[pad*2+1].data[1]==expected[bank*8+pad],"each pad follows next scale degree across bank boundaries");
    }
    p.set(1,0); in.clear();out.clear();in.midi(0,0xb7,2,19);p.run(in,out);
    in.clear();out.clear();in.midi(7,0x97,2,127);p.run(in,out);
    check(out.notes().size()==1&&out.notes()[0].data[1]==52&&out.notes()[0].data[2]==19,
        "scale mapping preserves cross-block hardware measured velocity");
    p.set(15,2);p.set(10,60);p.set(1,1);
    in.clear();out.clear();in.midi(2,0xa7,2,61);in.midi(8,0x87,2,0);p.run(in,out);
    check(out.notes().size()==2&&out.notes()[0].data[0]==0xa0&&out.notes()[0].data[1]==52&&out.notes()[1].data[1]==52,
        "scale/root/bank edits retain held pressure and release keys");
    const auto* state=static_cast<const clap_plugin_state_t*>(p.plugin->get_extension(p.plugin,CLAP_EXT_STATE));
    State saved;check(state->save(p.plugin,&saved.out)&&saved.data[4]==5&&saved.data.size()==94,"keyboard layouts use append-only v5 state");
    State bad;bad.data=saved.data;bad.data[27]=101;
    check(!state->load(p.plugin,&bad.in),"out-of-range scale state rejected");
    bad.cursor=0;bad.data=saved.data;bad.data[30]=128;
    check(!state->load(p.plugin,&bad.in),"invalid manual-note sentinel rejected");
    bad.cursor=0;bad.data=saved.data;bad.data.pop_back();
    check(!state->load(p.plugin,&bad.in),"truncated manual notes rejected");
    State same;check(state->save(p.plugin,&same.out)&&same.data==saved.data,"invalid keyboard state loads leave current state unchanged");
    saved.data[5]=0;saved.data[26]=2;saved.data[28]=2; // bank A, manual both units
    for(unsigned u=0;u<2;++u)std::copy(manual.begin(),manual.end(),saved.data.begin()+30+u*32);
    saved.data[30+31]=127;saved.data[30+32+3]=7; // nonsequential independent maps
    check(state->load(p.plugin,&saved.in),"manual layouts load repeated notes, OFF and full MIDI range");
    in.clear();out.clear();p.run(in,out); // Drain state-load releases before playing.
    in.clear();out.clear();in.midi(1,0x97,0,63);in.midi(2,0x97,1,80);in.midi(3,0x97,2,90);
    in.midi(4,0x87,0,0);in.midi(5,0xa7,1,40);in.midi(6,0x87,1,0);p.run(in,out);
    const auto notes=out.notes();
    check(notes.size()==4&&notes[0].data[1]==48&&notes[1].data[1]==48&&notes[2].data[0]==0xa0
        &&notes[3].data[0]==0x80&&notes[3].header.time==6,"duplicate manual pads keep the shared gate open until final release; OFF emits no note");
    State recalled;check(state->save(p.plugin,&recalled.out)&&recalled.data==saved.data,"independent manual layouts roundtrip exactly");
    State combined;combined.data=recalled.data;combined.data[20]=1;
    const auto cellNotes=nm::sequentialNotes(12);
    combined.data.insert(combined.data.begin()+21,cellNotes.begin(),cellNotes.end());
    check(state->load(p.plugin,&combined.in),"v5 can include both a custom cell map and independent manual keyboard maps");
    State combinedAgain;check(state->save(p.plugin,&combinedAgain.out)&&combinedAgain.data==combined.data,"combined cell/keyboard maps roundtrip exactly");
    p.set(6,1);p.set(11,1);p.set(13,2);p.set(7,0);
    in.clear();out.clear();in.midi(0,0x97,3,90,1);p.run(in,out);
    check(out.notes().size()==1&&out.notes()[0].data[0]==0x91&&out.notes()[0].data[1]==7,"unit two uses its own manual note map and channel");
    p.set(4,1);in.clear();out.clear();p.run(in,out);
    bool mapSeen[2] {};
    for(unsigned n=0;n<out.count;++n)if(out.events[n].type==CLAP_EVENT_MIDI_SYSEX) {
        uint8_t unit=0,channel=0;nm::PadNotes keys;
        if(nm::decodeKeyboardMap(out.events[n].sysex.buffer,out.events[n].sysex.size,unit,channel,keys))
            mapSeen[unit]=keys[2]==255 && keys[3]==(unit ? 7 : 51) && channel==(unit ? 1 : 0);
    }
    check(mapSeen[0]&&mapSeen[1],"resolved keyboard maps reach the LED owner separately for both units");
    p.set(14,1);p.set(15,3);p.set(10,96);p.set(1,3);
    in.clear();out.clear();in.midi(0,0x97,7,90);p.run(in,out);
    check(out.notes().empty(),"out-of-range scale pad never clamps or wraps to an unwanted note");
    p.set(15,1);out.clear();out.limit=0;p.run(in,out);
    in.clear();out.clear();p.run(in,out);
    bool retried=false;
    for(unsigned n=0;n<out.count;++n)if(out.events[n].type==CLAP_EVENT_MIDI_SYSEX) {
        uint8_t unit=0,channel=0;nm::PadNotes keys;
        retried |= nm::decodeKeyboardMap(out.events[n].sysex.buffer,out.events[n].sysex.size,unit,channel,keys)&&unit==0;
    }
    check(retried,"rejected keyboard LED maps retry on the following process block");
    if(trackerPath && neonPath) {
        Module tracker(trackerPath),neon(neonPath);tracker.activate();neon.set(5,1);neon.activate();
        p.set(1,0);p.set(10,48);p.set(14,1);p.set(15,31); // Minor pentatonic
        in.clear();out.clear();in.midi(5,0x97,1,83);in.midi(17,0x87,1,0);p.run(in,out);
        List through,result;tracker.run(out,through);
        bool mapPassed=false;
        for(unsigned n=0;n<through.count;++n)if(through.events[n].type==CLAP_EVENT_MIDI_SYSEX) {
            uint8_t unit=0,channel=0;nm::PadNotes keys;
            mapPassed |= nm::decodeKeyboardMap(through.events[n].sysex.buffer,through.events[n].sysex.size,unit,channel,keys)
                && unit==0 && keys==nm::keyboardNotes(1,48,31);
        }
        const auto passed=through.notes();
        check(mapPassed&&passed.size()==2&&passed[0].data[1]==51&&passed[0].data[2]==83
            &&passed[0].header.time==5&&passed[1].header.time==17,"actual Tracker preserves scaled notes and the resolved keyboard LED map");
        std::array<std::array<float,256>,32> audioData {};
        std::array<float*,32> channels;for(unsigned c=0;c<32;++c)channels[c]=audioData[c].data();
        clap_audio_buffer_t audio {};audio.channel_count=32;audio.data32=channels.data();
        neon.run(through,result,false,&audio);
    }
}
