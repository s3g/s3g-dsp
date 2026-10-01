void transportResetWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"transport test instance");
    auto& p=*self(plugin);loadFixture(p);check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"transport test activation");
    Audio a;clap_event_transport_t transport{};transport.flags=CLAP_TRANSPORT_HAS_TEMPO;transport.tempo=120;a.block.transport=&transport;
    setValue(p,Output,0);setValue(p,Crossfade,.5);
    for(unsigned d=0;d<2;++d){setValue(p,param(d,Amount),0);setValue(p,param(d,Clock),0);
        setValue(p,param(d,Repeat),1);setValue(p,param(d,Position),0);setValue(p,param(d,StackCycle),2);}
    const auto launch=[&](unsigned method){
        plugin->reset(plugin);transport.flags=CLAP_TRANSPORT_HAS_TEMPO;a.block.transport=&transport;
        for(unsigned d=0;d<2;++d){setValue(p,param(d,Playback),method);setValue(p,param(d,SourceMode),4);}
        a.run(plugin);for(unsigned d=0;d<2;++d)command(p,{CommandKind::Play,d});
        check(a.run(plugin,53)>1.e-7,"decks play while host stopped");
    };
    for(unsigned method=0;method<9;++method){launch(method);
        std::array<NeonTransportPosition,2> before{p.engine.transportPosition(0),p.engine.transportPosition(1)};
        plugin->stop_processing(plugin);plugin->reset(plugin);
        check(!p.engine.active(0)&&!p.engine.active(1),"CLAP reset really clears all running voices");
        check(p.transportRecovery[0].pending&&p.transportRecovery[1].pending,"only latched Free decks arm recovery");
        check(plugin->start_processing(plugin),"host restarts processing");
        transport.flags|=CLAP_TRANSPORT_IS_PLAYING|CLAP_TRANSPORT_IS_RECORDING;
        a.run(plugin);
        for(unsigned d=0;d<2;++d){
            const auto after=p.engine.transportPosition(d);
            check(p.playing[d].load()&&p.engine.noteCount(d)==1,"one fresh transport gesture per deck after Record");
            check(!p.transportRecovery[d].pending,"host recovery consumed once");
            check(control(p,d,Position)==0,"host recovery does not change the saved launch position");
            if(method!=0&&method!=8)check(after.ageFrames==before[d].ageFrames+Audio::frames,"Free technique and stack clocks continue instead of rewinding");
            if(method==0||method==6)check(std::abs(after.sourcePosition-before[d].sourcePosition)<.02,"Sample/Lanes source cursor continues");
            if(method==6)check(std::abs(after.lanePhase-before[d].lanePhase)<.02,"Lanes timeline continues");
        }
        check(a.run(plugin,40)>1.e-6,"all playback methods audible after host Record reset");
        plugin->reset(plugin);check(a.run(plugin,4)==0,"reset while host already playing stays silent");
    }
    launch(1);plugin->reset(plugin);check(a.run(plugin,3)==0,"reset without immediate host start stays silent");
    transport.flags|=CLAP_TRANSPORT_IS_PLAYING;check(a.run(plugin,3)==0,"expired reset intent cannot revive later");
    launch(1);plugin->reset(plugin);a.block.transport=nullptr;check(a.run(plugin,3)==0,"missing transport never authorizes recovery");
    for(unsigned cc:{120u,123u}){launch(1);plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
        a.midi(0xb0,cc,0);check(a.run(plugin,4)==0&&!p.transportLatched[0]&&!p.transportLatched[1],"same-block Panic wins over host recovery");}
    launch(1);plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
    command(p,{CommandKind::Stop,0});command(p,{CommandKind::Play,1});check(a.run(plugin,4)==0,"Stop and Play-to-stop win over queued recovery");
    launch(1);plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;p.forceStop.store(3);
    check(a.run(plugin,4)==0,"state-load stop mask cancels recovery");
    launch(1);plugin->reset(plugin);loadFixture(p);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
    check(a.run(plugin,4)==0,"source replacement cancels recovery");
    launch(1);plugin->reset(plugin);for(unsigned d=0;d<2;++d)setValue(p,param(d,Playback),2);
    transport.flags|=CLAP_TRANSPORT_IS_PLAYING;check(a.run(plugin,4)==0,"changed playback controls cancel stale recovery");
    launch(1);plugin->reset(plugin);setValue(p,Format,1);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
    check(a.run(plugin,4)==0,"changed output field cancels stale recovery");setValue(p,Format,0);
    launch(1);plugin->reset(plugin);plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
    check(a.run(plugin,4)==0,"a second reset does not retain old intent");
    launch(1);for(unsigned d=0;d<2;++d)setValue(p,param(d,Clock),1);plugin->reset(plugin);
    transport.flags|=CLAP_TRANSPORT_IS_PLAYING;check(a.run(plugin,4)==0,"Host-clock behavior is not overridden");
    for(unsigned d=0;d<2;++d)setValue(p,param(d,Clock),0);
    launch(1);for(unsigned d=0;d<2;++d){command(p,{CommandKind::Stop,d});setValue(p,param(d,PadMode),4);setValue(p,param(d,SamplerPads),1);command(p,{CommandKind::Pad,d,0,1});}
    check(a.run(plugin,20)>0,"keyboard voices sound before reset");plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;
    check(a.run(plugin,4)==0,"held keyboard notes are not resurrected");
    launch(1);command(p,{CommandKind::Stop,1});p.lastTake[0].store(0);command(p,{CommandKind::AuditionTake,0,0,1});a.run(plugin,4);
    plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;check(a.run(plugin,4)==0,"momentary take audition is not resurrected");
    launch(0);command(p,{CommandKind::Stop,1});setValue(p,param(0,SamplerPads),0);setValue(p,param(0,PadMode),4);
    command(p,{CommandKind::Pad,0,1,.3});a.run(plugin,30);
    plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;a.run(plugin,3);
    check(p.engine.transportPosition(0).layer==1&&std::abs(p.engine.transportPosition(0).velocity-.3f)<1.e-6,"layer launch retains its source and velocity");
    launch(0);command(p,{CommandKind::Stop,1});setValue(p,param(0,PadMode),1);command(p,{CommandKind::Pad,0,0,1});a.run(plugin,10);
    const auto loop=p.performance[0];plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;a.run(plugin,3);
    check(p.playing[0].load()&&p.performance[0].window&&p.performance[0].start==loop.start&&p.performance[0].end==loop.end,"latched loop retains its window");
    for(unsigned d=0;d<2;++d)setValue(p,param(d,Repeat),0);launch(0);a.run(plugin,250);
    plugin->reset(plugin);transport.flags|=CLAP_TRANSPORT_IS_PLAYING;check(a.run(plugin,4)==0,"finished non-looping samples do not restart");
    launch(1);plugin->reset(plugin);plugin->stop_processing(plugin);plugin->deactivate(plugin);
    check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"reactivate after reset");
    transport.flags|=CLAP_TRANSPORT_IS_PLAYING;check(a.run(plugin,4)==0,"deactivation cancels recovery");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}

void binaryTransportResetWorkflow(const clap_plugin_t* plugin,const clap_plugin_params_t* params,Audio& a){
    const auto set=[&](unsigned id,double value){clap_event_param_value_t e{};e.header={sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};e.param_id=id+1;e.value=value;
        clap_input_events_t in{&e,[](const clap_input_events_t*)->uint32_t{return 1;},[](const clap_input_events_t* in,uint32_t)->const clap_event_header_t*{return &static_cast<clap_event_param_value_t*>(in->ctx)->header;}};
        params->flush(plugin,&in,nullptr);};
    clap_event_transport_t t{};t.tempo=120;a.block.transport=&t;
    set(Crossfade,0);set(param(0,Clock),0);set(param(0,Repeat),1);set(param(0,Position),0);set(param(0,Amount),0);
    for(unsigned method=0;method<9;++method){
        plugin->reset(plugin);t.flags=CLAP_TRANSPORT_HAS_TEMPO;set(param(0,Playback),method);a.run(plugin);
        a.midi(0x90,0x60,127);check(a.run(plugin,40)>1.e-6,"exact bundle plays before host Record");
        plugin->stop_processing(plugin);plugin->reset(plugin);check(plugin->start_processing(plugin),"exact bundle restarts processing");
        t.flags|=CLAP_TRANSPORT_IS_PLAYING|CLAP_TRANSPORT_IS_RECORDING;
        check(a.run(plugin,40)>1.e-6,"exact bundle recovers every playback method after host Record reset");
        a.midi(0xb0,120,0);a.run(plugin,4);check(a.run(plugin,4)==0,"exact bundle still honors Panic");
    }
    a.block.transport=nullptr;
}
