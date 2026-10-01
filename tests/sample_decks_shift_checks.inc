void beatpadShiftWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"Shift plugin init");auto& p=*self(plugin);loadFixture(p);
    check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"Shift plugin activation");Audio a;a.run(plugin);
    const auto send=[&](unsigned status,unsigned key,unsigned v,unsigned blocks=1){a.midi(status,key,v);a.run(plugin,blocks);};
    const auto sweep=[&](unsigned d,unsigned key){send(0xb0+d,key,0);send(0xb0+d,key,127);};
    for(unsigned d=0;d<2;++d){
        send(0xb0+d,0x4b,0);check(control(p,d,Gain)==-24,"first ordinary knob movement retains direct control");
        send(0x90+d,0x1c,127);send(0xb0+d,0x0b,1);
        check(control(p,d,StackCycle)==4&&control(p,d,Gain)==-24,"Shift waits for stack-cycle pickup and leaves gain alone");
        send(0xb0+d,0x0b,127);check(control(p,d,StackCycle)==30,"crossing shifted pickup reaches full cycle range");
        send(0x80+d,0x1c,0);send(0xb0+d,0x4b,126);
        check(control(p,d,Gain)==-24,"releasing Shift cannot jump mixer gain");
        send(0xb0+d,0x4b,0);send(0xb0+d,0x4b,64);check(control(p,d,Gain)==0,"returning through pickup restores normal gain control");
        setValue(p,param(d,Gain),6);send(0xb0+d,0x4b,65);check(control(p,d,Gain)==6,"external parameter edit re-arms pickup");
        send(0xb0+d,0x4b,96);check(control(p,d,Gain)>6,"crossing new software value takes control again");
        setValue(p,param(d,Clock),0);sweep(d,0x0c);check(control(p,d,Cycle)==30,"second shifted knob controls source seconds");
        setValue(p,param(d,Clock),1);send(0xb0+d,0x0b,126);check(control(p,d,StackBeats)==8,"clock change requires fresh beat pickup");
        sweep(d,0x0b);sweep(d,0x0c);check(control(p,d,StackBeats)==32&&control(p,d,CycleBeats)==32,"host clock maps both cycle knobs to beats");
        check(control(p,d,StackCycle)==30&&control(p,d,Cycle)==30,"host cycle knobs preserve free-time parameters");
        setValue(p,param(d,Clock),0);
        const unsigned macros[]{Tune,family(NeonFamily::MotionWindow),GrainSize,Technique2,Tune,Technique0,
            family(NeonFamily::LaneSlew),family(NeonFamily::SpectralBlur),family(NeonFamily::CutRepeat)};
        send(0xb4,0x47,127);
        for(unsigned method=0;method<9;++method){setValue(p,param(d,Playback),method);sweep(d,0x0d);
            check(control(p,d,macros[method])==float(p.defs[param(d,macros[method])].hi),"third shifted knob controls the documented playback macro");}
        setValue(p,param(d,Playback),5);setValue(p,param(d,family(NeonFamily::WavesetEngine)),1);sweep(d,0x0d);
        check(control(p,d,family(NeonFamily::OscGroup))==32,"oscillator wavesets macro controls oscillator group");
        sweep(d,0x0e);check(control(p,d,Amount)==1,"fourth classic-EQ knob controls FX amount");
        send(0xb4,0x48,127);setValue(p,param(d,Playback),2);setValue(p,param(d,GrainSize),80);setValue(p,param(d,Amount),.5);
        sweep(d,0x0e);check(control(p,d,GrainSize)==500&&control(p,d,Amount)==.5,"filter-EQ third physical knob remains the playback macro");
        sweep(d,0x09);check(control(p,d,Amount)==1,"filter-EQ fourth physical knob controls FX amount");
        const float filter=control(p,d,Filter);send(0xb0+d,0x49,126);check(control(p,d,Filter)==filter,"returning from shifted filter needs pickup");
        send(0xb0+d,0x49,64);send(0xb0+d,0x49,0);check(control(p,d,Filter)==-1,"filter pickup preserves LPF endpoint");
        send(0xb4,0x47,127);
    }
    // Changing macro while held must not transfer the previous knob's latch.
    setValue(p,param(0,Playback),1);sweep(0,0x0d);setValue(p,param(0,Playback),2);setValue(p,param(0,GrainSize),80);
    send(0xb0,0x0d,126);check(control(p,0,GrainSize)==80,"new playback method waits for its own macro pickup");
    // Separate decks, role switches and octave changes retain held-note ownership.
    for(unsigned d=0;d<2;++d){setValue(p,param(d,Playback),0);setValue(p,param(d,Tune),0);setValue(p,param(d,Amount),0);
        setValue(p,param(d,Filter),0);setValue(p,param(d,Layer),0);setValue(p,param(d,SourceMode),1);
        setValue(p,param(d,SamplerPads),0);setValue(p,param(d,PadMode),0);setValue(p,param(d,KeyboardOctave),0);
        send(0x90+d,0x0c,127);check(keyboardPads(p,d),"Shift Sampler selects Keys and the sampler page");
        send(0x90+d,0x08,127);check(keyboardPads(p,d),"Shift Hot Cue is reserved for firmware velocity toggle");
        send(0x92+d,0x20,127);const auto held=p.padNotes[d][0];check(held.key==60&&held.id,"keyboard starts root note");
        send(0x90+d,0x02,127);check(control(p,d,KeyboardOctave)==1&&p.padNotes[d][0].key==60,"octave shift leaves a held pitch unchanged");
        send(0x92+d,0x21,127);check(p.padNotes[d][1].key==73,"next key uses new octave");
        send(0x90+d,0x0c,127);check(!keyboardPads(p,d),"Shift Sampler returns to layers");
        send(0x82+d,0x20,0);send(0x82+d,0x21,0,50);check(p.engine.noteCount(d)==0,"role change still releases original keyboard notes");
        send(0x90+d,0x01,127);check(control(p,d,KeyboardOctave)==1,"shifted bend does not transpose outside keyboard mode");
        send(0x90+d,0x0c,127);setValue(p,param(d,KeyboardOctave),4);send(0x90+d,0x02,127);
        check(control(p,d,KeyboardOctave)==4,"octave up is bounded");setValue(p,param(d,KeyboardOctave),-4);send(0x90+d,0x01,127);
        check(control(p,d,KeyboardOctave)==-4,"octave down is bounded");
    }
    // Store is independent of the on-screen Jump/Store/Clear menu and velocity.
    plugin->reset(plugin);setValue(p,param(0,Playback),1);setValue(p,param(0,Layer),1);setValue(p,param(0,Position),.37);
    setValue(p,param(0,StackNavigation),1);setValue(p,param(0,StackPosition),.35);setValue(p,param(0,CueAction),2);a.run(plugin);
    const auto historyBefore=p.undo.size();send(0x90,0x1c,127);send(0x92,0x42,127);service(p);
    const auto cue=p.document->decks[0].bookmarks[2];
    check(cue.set&&cue.layer==1&&std::abs(cue.position-.37)<1.e-5&&std::abs(cue.stackPosition-.7)<1.e-5,"Shift cue pad stores fractional stack bookmark");
    check(!p.playing[0].load()&&control(p,0,CueAction)==2&&p.undo.size()==historyBefore+1,"store never auditions and is undoable without changing Cue Action");
    send(0x80,0x1c,0);send(0x82,0x02,0);check(!p.playing[0].load(),"unshifted release after store cannot recall cue");
    history(p,false);a.run(plugin);check(!p.document->decks[0].bookmarks[2].set,"Undo reverses hardware cue store");
    history(p,true);a.run(plugin);check(p.document->decks[0].bookmarks[2].set,"Redo restores hardware cue store");
    setValue(p,param(0,SourceMode),4);setValue(p,param(0,StackNavigation),0);setValue(p,param(0,CueAction),0);command(p,{CommandKind::Play,0});a.run(plugin,20);
    const double position=control(p,0,Position);const unsigned voices=p.engine.noteCount(0);
    send(0x92,0x43,127);service(p);send(0x82,0x03,0);
    check(p.playing[0].load()&&p.engine.noteCount(0)==voices&&control(p,0,Position)==position,"store during playback does not retrigger or seek");
    check(p.document->decks[0].bookmarks[3].set,"shifted pad address works even without a separate Shift press");
    setValue(p,param(0,Layer),31);a.run(plugin);send(0x92,0x43,127);service(p);send(0x82,0x43,0);
    check(p.document->decks[0].bookmarks[3].set,"empty source cannot erase existing cue through Store");
    // Fader start uses hardware PLAY but must never toggle a running deck off.
    plugin->reset(plugin);setValue(p,param(0,Playback),0);setValue(p,param(0,Layer),0);setValue(p,param(0,SourceMode),1);
    setValue(p,param(0,StackNavigation),0);setValue(p,param(0,CueLayer),1);setValue(p,param(0,CueStackPosition),-1);setValue(p,param(0,CuePosition),.25);
    setValue(p,param(0,Position),0);a.run(plugin);send(0x90,0x60,127,20);const double before=currentPosition(p,0);
    send(0x90,0x1c,127);send(0x90,0x60,127);
    check(p.playing[0].load()&&control(p,0,Layer)==0&&currentPosition(p,0)>before,"Shift fader up does not stop or rec cue on a running deck");
    send(0x90,0x73,127);check(!p.playing[0].load()&&control(p,0,Layer)==1&&control(p,0,Position)==.25,"Shift fader down returns to stored cue without playing");
    send(0x90,0x60,127);check(p.playing[0].load()&&currentPosition(p,0)>.25,"Shift fader up launches a stopped deck from cue");
    send(0x90,0x60,127);check(p.playing[0].load(),"duplicate fader start remains idempotent");
    send(0x80,0x1c,0);send(0xb0,0x4f,0);check(!p.playing[0].load()&&control(p,0,Position)==.25,"armed fader return works after releasing Shift");
    send(0x90,0x60,127);send(0xb0,0x4f,0);send(0x90,0x73,127);
    check(p.playing[0].load(),"ordinary fader movement does not stop playback");
    setValue(p,param(0,CueLayer),31);send(0x90,0x1c,127);send(0x90,0x73,127);
    check(!p.playing[0].load(),"fader return still stops when stored cue source was cleared");
    send(0x80,0x1c,0);setValue(p,param(0,CueLayer),1);send(0x90,0x60,127);
    // Bend shares the damped motor; both directions cancel and release is owned
    // by the physical button, not the current Shift layer or pad page.
    setValue(p,param(0,Rate),1);send(0x90,0x42,127,150);
    check(std::abs(p.platterRates[0].back()-1.08)<1.e-5&&control(p,0,Rate)==1,"held bend reaches +8% without changing saved rate");
    send(0x90,0x41,127,150);check(std::abs(p.platterRates[0].back()-1)<1.e-5,"opposite held bend buttons cancel");
    send(0x80,0x02,0,150);check(std::abs(p.platterRates[0].back()-.92)<1.e-5,"shifted release releases original bend direction");
    send(0x80,0x01,0,150);check(std::abs(p.platterRates[0].back()-1)<1.e-5,"bend release smoothly restores rate");
    send(0x90,0x42,127);send(0xb0,123,0);check(!p.bendHeld[0][1],"all notes off clears held bend");
    for(unsigned method=0;method<9;++method){plugin->reset(plugin);setValue(p,param(0,Playback),method);a.run(plugin);
        send(0x90,0x60,127);send(0x90,0x42,127,40);
        check(p.playing[0].load()&&p.scanMix[0]==1&&control(p,0,Rate)==1,"every method retains its voice and receives held rim-style scan");
        send(0x80,0x42,0,150);check(p.scanMix[0]==0&&p.playing[0].load(),"every method returns to native scan after bend release");}
    send(0x90,0x42,127);setValue(p,Controller,0);a.run(plugin,150);
    check(!p.bendHeld[0][1]&&std::abs(p.platterRates[0].back()-1)<1.e-5,"controller profile switch releases hardware bend");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}

void beatpadShiftBinaryWorkflow(const clap_plugin_t* plugin,const clap_plugin_params_t* params,Audio& a){
    const auto set=[&](unsigned index,double value){clap_event_param_value_t e{};e.header={sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};e.param_id=index+1;e.value=value;
        clap_input_events_t in{&e,[](const clap_input_events_t*)->uint32_t{return 1;},[](const clap_input_events_t* in,uint32_t)->const clap_event_header_t*{return &static_cast<clap_event_param_value_t*>(in->ctx)->header;}};params->flush(plugin,&in,nullptr);};
    const auto get=[&](unsigned index){double v=0;check(params->get_value(plugin,index+1,&v),"exact binary parameter read");return v;};
    plugin->reset(plugin);set(Controller,1);set(param(0,Gain),0);set(param(0,Clock),0);set(param(0,StackCycle),4);
    a.midi(0xb0,0x4b,64);a.run(plugin);a.midi(0xb0,0x0b,0);a.run(plugin);a.midi(0xb0,0x0b,127);a.run(plugin);
    check(get(param(0,StackCycle))==30&&get(param(0,Gain))==0,"exact binary shifted knob controls cycle only");
    a.midi(0xb0,0x4b,126);a.run(plugin);check(get(param(0,Gain))==0,"exact binary mixer return uses pickup");
    set(param(0,SamplerPads),0);set(param(0,KeyboardOctave),0);a.midi(0x90,0x0c,127);a.run(plugin);
    a.midi(0x90,0x02,127);a.run(plugin);check(get(param(0,SamplerPads))==1&&get(param(0,PadMode))==4&&get(param(0,KeyboardOctave))==1,"exact binary keyboard shortcut and octave mapping");
    set(param(0,Playback),0);set(param(0,SourceMode),1);set(param(0,CueLayer),0);set(param(0,CuePosition),.23);set(param(0,Position),0);
    a.midi(0x90,0x1c,127);a.midi(0x90,0x60,127);a.run(plugin);a.midi(0x90,0x73,127);a.run(plugin);
    check(std::abs(get(param(0,Position))-.23)<1.e-6,"exact binary shifted fader returns to cue");
    plugin->reset(plugin);
}
