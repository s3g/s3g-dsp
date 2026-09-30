void captureDestinationWorkflow(Bytes& mixedState){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"capture destination plugin");
    auto& p=*self(plugin);loadFixture(p);check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"capture destination activate");Audio a;
    setValue(p,Format,0);setValue(p,RecordSource,0);setValue(p,RecordSeconds,1);setValue(p,param(1,Layer),7);a.run(plugin);
    command(p,{CommandKind::Play,0});a.run(plugin,4);startRecord(p,0);
    check(p.capture.state.load()==1&&p.capture.to==1&&p.capture.target==7&&p.capture.width==2,"resample prefers selected empty receiving layer");
    a.run(plugin,4);startRecord(p,0);a.run(plugin);service(p);waitWork(p);
    check(p.document->decks[1].layers[7].asset&&p.document->decks[1].layers[7].asset->channelCount==2,"selected destination holds output-width take");

    for(unsigned existingWidth:{1u,4u,8u,16u}){
        reset(plugin);auto doc=std::make_shared<Document>();
        doc->decks[0].layers[0].asset=tone(2);doc->decks[1].layers[0].asset=tone(existingWidth);
        analyzeLayer(doc->decks[0].layers[0]);analyzeLayer(doc->decks[1].layers[0]);
        publish(p,std::move(doc),3,false);setValue(p,param(0,Layer),0);setValue(p,param(0,Playback),0);setValue(p,param(0,SourceMode),1);
        setValue(p,param(1,Layer),2);setValue(p,Format,0);setValue(p,RecordSource,0);a.run(plugin);
        const auto original=p.document->decks[1].layers[0].asset;
        command(p,{CommandKind::Play,0});a.run(plugin,4);setValue(p,param(0,PadMode),7);
        command(p,{CommandKind::Pad,0,4,1});a.run(plugin);service(p);
        check(p.capture.state.load()==1&&p.capture.target==2&&p.capture.width==2,"Capture/Handoff accepts stereo into a different-width stack's empty layer");
        command(p,{CommandKind::Pad,0,4,0});a.run(plugin,8);command(p,{CommandKind::Pad,0,4,1});a.run(plugin);service(p);a.run(plugin);service(p);waitWork(p);
        check(p.document->decks[1].layers[0].asset==original&&p.document->decks[1].layers[2].asset->channelCount==2,"recording preserves existing channel format and stores new take as stereo");
        for(unsigned method=0;method<9;++method){reset(plugin);setValue(p,param(1,Layer),2);setValue(p,param(1,Playback),method);
            setValue(p,param(1,SourceMode),1);setValue(p,param(1,StackNavigation),0);setValue(p,Crossfade,1);a.run(plugin);
            command(p,{CommandKind::Play,1});check(a.run(plugin,8)>1.e-7,"new stereo take auditions through every playback method in a mixed-width stack");}
        Bytes state;check(saveState(plugin,&state.out)&&loadState(plugin,&state.in),"mixed-width capture state reloads");
        check(p.document->decks[1].layers[0].asset->channelCount==existingWidth&&p.document->decks[1].layers[2].asset->channelCount==2,
            "state recall keeps native channel counts, without converting older layers");
        reset(plugin);setValue(p,param(1,Layer),0);setValue(p,param(1,Playback),0);setValue(p,param(1,SourceMode),1);
        setValue(p,Format,existingWidth==1?0:existingWidth==4?1:existingWidth==8?2:5);a.run(plugin);
        command(p,{CommandKind::Play,1});check(a.run(plugin,8)>1.e-7,"original native-format layer still plays after stereo capture");
        const unsigned width=existingWidth==1?2:existingWidth;
        for(unsigned ch=1;ch<width;++ch)for(unsigned n=0;n<256;++n)
            check(std::abs(a.pcm[ch][n]-(existingWidth==1?a.pcm[0][n]:ch%2?-a.pcm[0][n]:a.pcm[0][n]))<1.e-5,"native mono/spatial field stays coherent after switching format groups");
        mixedState.data=std::move(state.data);mixedState.at=0;
    }
    // Two stereo sources separated by other formats share a sparse format
    // group. Even-numbered source IDs must not share one Wavesets renderer.
    reset(plugin);auto sparse=std::make_shared<Document>();
    for(unsigned n:{0u,2u,4u}){auto& l=sparse->decks[0].layers[n];l.asset=tone(n==0?8:2);analyzeLayer(l);}
    publish(p,std::move(sparse),3,false);setValue(p,param(0,Layer),2);setValue(p,Format,0);setValue(p,Crossfade,0);
    const auto& group=p.document->decks[0].playbackStack(2);const auto blend=neonStackBlend(.75,&group);
    check(group.skipEmptyLayers&&blend.first==2&&blend.second==4&&blend.mix==.5f,"sparse format group blends matching native channels without renumbering layers");
    for(unsigned method:{1u,2u,4u,5u,6u,7u}){reset(plugin);setValue(p,param(0,Playback),method);setValue(p,param(0,SourceMode),4);
        setValue(p,param(0,StackNavigation),1);setValue(p,param(0,StackPosition),.75);a.run(plugin);command(p,{CommandKind::Play,0});
        check(a.run(plugin,32)>1.e-7,"stack scan stays audible across filtered-format gaps");
        bool first=false,last=false;for(unsigned n=0;n<p.engine.cursorCount(0);++n){const auto& cursor=p.engine.cursors(0)[n];
            check(cursor.layer==2||cursor.layer==4,"mixed-format stack scan never interprets octo as stereo");first|=cursor.layer==2;last|=cursor.layer==4;}
        check(first&&last,"scan publishes both same-format layers across gaps");}
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}

void binaryCaptureDestinationWorkflow(const clap_plugin_t* plugin,const clap_plugin_params_t* params,Audio& a,Bytes& state){
    const auto* stateExt=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    plugin->reset(plugin);state.at=0;check(stateExt->load(plugin,&state.in),"exact binary loads mixed native-format captures");a.run(plugin);
    const auto set=[&](unsigned key,double v){clap_event_param_value_t event{};
        event.header={sizeof(event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};event.param_id=key+1;event.value=v;
        clap_input_events_t input{&event,[](const clap_input_events_t*)->uint32_t{return 1;},
            [](const clap_input_events_t* e,uint32_t)->const clap_event_header_t*{return &static_cast<clap_event_param_value_t*>(e->ctx)->header;}};
        params->flush(plugin,&input,nullptr);};
    set(Format,0);set(Crossfade,1);set(param(1,Playback),0);set(param(1,Layer),2);set(param(1,SourceMode),1);set(param(1,Position),0);a.run(plugin);
    a.midi(0x91,0x60,127);check(a.run(plugin,8)>1.e-7,"exact binary auditions stereo take beside 16-channel original");
    set(param(0,Layer),7);set(param(1,PadMode),7);set(RecordSource,0);a.run(plugin);
    a.midi(0x93,0x3c,127);a.run(plugin,3);plugin->on_main_thread(plugin);a.midi(0x83,0x3c,0);a.run(plugin,8);
    a.midi(0x93,0x3c,127);a.run(plugin,3);plugin->on_main_thread(plugin);a.run(plugin);plugin->on_main_thread(plugin);
    Bytes saved;bool ready=false;
    for(unsigned n=0;n<3000&&!ready;++n){plugin->on_main_thread(plugin);saved.data.clear();ready=stateExt->save(plugin,&saved.out);if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(ready,"exact binary capture commits");
    const auto* inspect=create(&factory,&testHost,descriptor.id);check(inspect&&inspect->init(inspect)&&loadState(inspect,&saved.in),"inspect exact binary take");
    const auto& take=self(inspect)->document->decks[0].layers[7];
    check(take.asset&&take.asset->channelCount==2&&take.asset->frameCount()>0,"exact binary Capture/Handoff honors selected empty receiving layer and output format");
    inspect->destroy(inspect);
}
