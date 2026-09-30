// A cue is a source address (layer + time), not an index in a cursor array.
void cueLayerWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"cue layer plugin");
    auto& p=*self(plugin);loadFixture(p);
    check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"cue layer activation");Audio a;
    const auto prepare=[&](unsigned method,unsigned layer){reset(plugin);setValue(p,param(0,Playback),method);
        setValue(p,param(0,Layer),layer);setValue(p,param(0,SourceMode),1);setValue(p,param(0,StackNavigation),0);
        setValue(p,param(0,Start),p.document->decks[0].layers[layer].start);setValue(p,param(0,End),p.document->decks[0].layers[layer].end);
        setValue(p,param(0,Position),.37);setValue(p,param(0,PadMode),0);setValue(p,param(0,CueAction),0);
        a.run(plugin);};
    const auto store=[&](unsigned pad){command(p,{CommandKind::StoreBookmark,0,pad});a.run(plugin);service(p);};
    prepare(6,0);setValue(p,param(0,StackNavigation),1);setValue(p,param(0,StackPosition),.08);
    command(p,{CommandKind::Play,0});a.run(plugin,24);
    check(p.engine.cursorCount(0)>=2&&p.engine.cursors(0)[p.engine.cursorCount(0)-1].layer==1,
        "cue regression fixture blends L01 with a quieter L02 last in the cursor list");
    double dominantPosition=-1;
    for(unsigned i=0;i<p.engine.cursorCount(0);++i)if(p.engine.cursors(0)[i].layer==0)dominantPosition=p.engine.cursors(0)[i].sourcePositionNormalized;
    store(0);const auto blend=p.document->decks[0].bookmarks[0];
    check(blend.set&&blend.layer==0&&std::abs(blend.position-dominantPosition)<1.e-6,
        "cue stores the dominant lane and that same lane's cursor, not the last fading cursor");

    prepare(0,0);command(p,{CommandKind::Play,0});a.run(plugin,8);
    setValue(p,param(0,Layer),2);setValue(p,param(0,Position),.63);store(1);
    check(p.document->decks[0].bookmarks[1].layer==2&&std::abs(p.document->decks[0].bookmarks[1].position-.63)<1.e-6,
        "changing layer then storing before the next render never uses the previous layer's cursor");
    auto trimmed=std::make_shared<Document>(*p.document);trimmed->decks[0].layers[2].start=.2;trimmed->decks[0].layers[2].end=.8;
    publish(p,std::move(trimmed),1,false);prepare(0,0);
    setValue(p,param(0,Layer),2);setValue(p,param(0,Position),.5);store(4);
    check(p.document->decks[0].bookmarks[4].layer==2&&std::abs(p.document->decks[0].bookmarks[4].position-.5)<1.e-6,
        "same-block layer change uses the new layer's saved trim, not old Start/End controls");
    trimmed=std::make_shared<Document>(*p.document);trimmed->decks[0].layers[2].start=0;trimmed->decks[0].layers[2].end=1;
    publish(p,std::move(trimmed),1,false);

    for(unsigned method=0;method<9;++method){prepare(method,2);store(2);
        check(p.document->decks[0].bookmarks[2].layer==2,"stopped cue stores selected layer in every method");
        setValue(p,param(0,Layer),0);a.run(plugin);
        command(p,{CommandKind::Pad,0,2,1});a.run(plugin,4);
        bool found=false;for(unsigned i=0;i<p.engine.cursorCount(0);++i){const auto& head=p.engine.cursors(0)[i];
            found|=head.layer==2&&head.sourceAsset==p.document->decks[0].layers[2].asset.get();}
        check(found,"cue recall actually renders the saved layer in every method");
        command(p,{CommandKind::Pad,0,2,0});a.run(plugin);
    }
    // Temporary one-source slice stacks number their internal layer zero.
    // The saved cue must retain the document layer, not that private index.
    prepare(0,2);setValue(p,param(0,PadMode),3);setValue(p,param(0,SliceHold),1);
    command(p,{CommandKind::Pad,0,3,1});a.run(plugin,4);store(3);
    check(p.document->decks[0].bookmarks[3].layer==2&&p.document->decks[0].bookmarks[3].position>.375,
        "cue in a slice window maps back to its real layer");

    // A held ride is a temporary navigation override, not part of the cue.
    prepare(0,0);command(p,{CommandKind::Play,0});a.run(plugin);
    setValue(p,param(0,PadMode),6);command(p,{CommandKind::Pad,0,1,1});a.run(plugin,120);
    setValue(p,param(0,PadMode),0);command(p,{CommandKind::Pad,0,2,1});a.run(plugin,4);
    check(control(p,0,Layer)==2&&!p.performance[0].riding&&newest(p.performance[0].rideOrder)<0,
        "cue recall cancels Stack Ride instead of immediately overriding the bookmarked layer");
    for(unsigned n=0;n<p.engine.cursorCount(0);++n)
        check(p.engine.cursors(0)[n].sourceAsset==p.document->decks[0].layers[2].asset.get(),"cue wins over held Stack Ride in rendered audio");

    for(unsigned method=0;method<9;++method){prepare(method,2);
        command(p,{CommandKind::SetCue,0});a.run(plugin);
        check(control(p,0,CueLayer)==2&&std::abs(control(p,0,CuePosition)-.37)<1.e-6,
            "main SET CUE stores layer and stopped position in every method");
        setValue(p,param(0,Layer),0);setValue(p,param(0,SourceMode),0);a.run(plugin);
        command(p,{CommandKind::Cue,0});a.run(plugin,4);
        bool found=false;for(unsigned n=0;n<p.engine.cursorCount(0);++n){const auto& head=p.engine.cursors(0)[n];
            found|=head.layer==2&&head.sourceAsset==p.document->decks[0].layers[2].asset.get();}
        check(control(p,0,Layer)==2&&found,"main CUE recalls its layer despite a different edit/primary selection");
    }
    // Exercise the hardware Store + pad path, including the delayed velocity CC.
    prepare(0,1);setValue(p,param(0,CueAction),1);a.midi(0x92,0x00,127);a.midi(0xb2,0x00,85,1);a.run(plugin,2);service(p);
    check(p.document->decks[0].bookmarks[0].layer==1&&control(p,0,CueAction)==0,"Beatpad Store saves the target layer and returns to Jump");
    a.midi(0x82,0x00,0);a.run(plugin);
    Bytes state;check(saveState(plugin,&state.out),"save layer cues");
    check(loadState(plugin,&state.in)&&p.document->decks[0].bookmarks[0].layer==1
        &&p.document->decks[0].bookmarks[1].layer==2&&control(p,0,CueLayer)==2,"pad and main layer cues survive project recall");
    Bytes legacy;legacy.data=state.data;legacy.data[4]=6;
    const float reserved=0;for(unsigned d=0;d<2;++d)std::memcpy(legacy.data.data()+12+param(d,CueLayer)*sizeof(float),&reserved,sizeof(float));
    check(loadState(plugin,&legacy.in)&&control(p,0,CueLayer)==-1&&p.document->decks[0].bookmarks[0].layer==1,
        "v6 sets keep their pad cues and migrate main cues without inventing a layer");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}

void binaryCueLayerWorkflow(const clap_plugin_t* plugin,const clap_plugin_params_t* params,Audio& a){
    const auto set=[&](unsigned key,double v){clap_event_param_value_t event{};
        event.header={sizeof(event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};event.param_id=key+1;event.value=v;
        clap_input_events_t input{&event,[](const clap_input_events_t*)->uint32_t{return 1;},
            [](const clap_input_events_t* e,uint32_t)->const clap_event_header_t*{return &static_cast<clap_event_param_value_t*>(e->ctx)->header;}};
        params->flush(plugin,&input,nullptr);};
    const auto get=[&](unsigned key){double v=0;check(params->get_value(plugin,key+1,&v),"read exact binary cue parameter");return v;};
    plugin->reset(plugin);set(Controller,1);set(param(0,Playback),0);set(param(0,Layer),2);
    set(param(0,SourceMode),1);set(param(0,StackNavigation),0);set(param(0,Position),.43);a.run(plugin);
    a.midi(0x90,0x5e,127);a.run(plugin);
    check(get(param(0,CueLayer))==2&&std::abs(get(param(0,CuePosition))-.43)<1.e-6,"exact binary Beatpad main Cue stores layer/time");
    set(param(0,Layer),0);set(param(0,Position),.17);a.run(plugin);
    a.midi(0x90,0x5f,127);check(a.run(plugin,4)>1.e-6,"exact binary cue renders audio");
    check(get(param(0,Layer))==2&&std::abs(get(param(0,Position))-.43)<1.e-6,"exact binary Cue Play returns to stored layer/time");
    plugin->reset(plugin);set(param(0,Layer),1);set(param(0,PadMode),0);set(param(0,CueAction),1);a.run(plugin);
    a.midi(0x92,0x03,127);a.run(plugin,3);plugin->on_main_thread(plugin);a.midi(0x82,0x03,0);a.run(plugin);
    set(param(0,Layer),0);a.run(plugin);a.midi(0x92,0x03,127);a.run(plugin,3);
    check(get(param(0,Layer))==1,"exact binary hardware Cues pad returns to stored lane");
}
