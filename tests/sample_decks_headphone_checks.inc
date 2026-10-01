void headphoneMatrixChecks(){
    DeckHeadphones hp;hp.prepare(48000);
    for(unsigned format=0;format<6;++format){const unsigned width=sampleNeonBusWidth(decksLayout(format));
        for(unsigned fold=0;fold<3;++fold){const auto m=hp.matrix(format,fold);
            for(const auto& row:m){float sum=0;for(unsigned ch=0;ch<width;++ch){check(std::isfinite(row[ch]),"cue matrix finite");sum+=std::abs(row[ch]);}
                check(sum<=1.00001f&&sum>0,"cue matrix bounds correlated-channel gain");}
            if(format==0)check(m[0][0]==1&&m[1][1]==1&&m[0][1]==0&&m[1][0]==0,"stereo cue preserves L/R without summing");
            if(format==1||format==2)for(unsigned ch=0;ch<width;++ch)
                check(std::abs(m[0][ch])+std::abs(m[1][ch])>0,"multichannel cue includes every discrete channel");
            if(format>=3){check(std::abs(m[0][0]-m[1][0])<1.e-6&&m[0][0]>0,"ambisonic W decodes centered");
                check(m[0][1]*m[1][1]<0,"ambisonic side component decodes with opposite L/R signs");}
        }
        const unsigned cueFirst=(width+1)&~1u;
        check(deckCueOutput(1,width,false)==int(cueFirst),"automatic cue follows main format on an odd/even pair");
        check(deckCueOutput(1,width,true)==int((width*(width*3<=32?3:1)+1)&~1u),"automatic cue preserves fixed stem layout");
        for(unsigned selection=0;selection<18;++selection)for(bool stems:{false,true}){
            const int out=deckCueOutput(selection,width,stems);const unsigned used=width*(stems&&width*3<=32?3:1);
            check(out<0||(unsigned(out)>=used&&out+1<32),"cue routing cannot collide with main or stems");}
        constexpr unsigned frames=256;
        std::array<std::array<float,frames>,32> samples{};
        std::array<std::array<float,frames>,16> a{},b{};std::array<float*,32> output{};std::array<const float*,16> pa{},pb{};
        for(unsigned ch=0;ch<32;++ch){output[ch]=samples[ch].data();samples[ch].fill(ch<width?.1f:0);}
        for(unsigned ch=0;ch<width;++ch){a[ch].fill(.6f);b[ch].fill(.2f);pa[ch]=a[ch].data();pb[ch]=b[ch].data();}
        DeckHeadphoneSettings settings;settings.format=format;settings.selected={true,true};settings.gainDb=0;hp.reset();
        for(unsigned n=0;n<32;++n)hp.render(settings,width,pa.data(),pb.data(),output.data(),frames);
        const auto m=hp.matrix(format,0);
        for(unsigned ear=0;ear<2;++ear){float expected=0;for(unsigned ch=0;ch<width;++ch)expected+=m[ear][ch]*.4f;
            check(std::abs(samples[cueFirst+ear].back()-expected)<.00002,"two selected decks are averaged before stereo decode");}
        for(unsigned ch=0;ch<width;++ch)for(float v:samples[ch])check(v==.1f,"cue leaves every main sample bit-identical");
        settings.mix=1;for(unsigned n=0;n<32;++n)hp.render(settings,width,pa.data(),pb.data(),output.data(),frames);
        for(unsigned ear=0;ear<2;++ear){float expected=0;for(unsigned ch=0;ch<width;++ch)expected+=m[ear][ch]*.1f;
            check(std::abs(samples[cueFirst+ear].back()-expected)<.00002,"master endpoint monitors actual main mix");}
        settings.output=2;const auto before=samples;hp.render(settings,width,pa.data(),pb.data(),output.data(),frames);
        check(samples==before,"conflicting route makes no output writes");
        settings.output=17;settings.mix=0;for(auto& row:a)row.fill(100);for(auto& row:b)row.fill(100);
        hp.render(settings,width,pa.data(),pb.data(),output.data(),frames);
        for(unsigned ch=30;ch<32;++ch)for(float v:samples[ch])check(std::isfinite(v)&&std::abs(v)<=1,"headphone-only safety ceiling bounds overload");
    }
}

void headphoneWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"headphone plugin");auto& p=*self(plugin);loadFixture(p);
    check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"headphone activation");Audio a;
    setValue(p,param(0,Level),0);setValue(p,Crossfade,1);command(p,{CommandKind::Play,0});a.run(plugin,10);
    for(const auto& channel:a.pcm)for(float v:channel)check(v==0,"cue is silent until selected");
    a.midi(0x90,0x61,127);a.midi(0x80,0x61,0);a.run(plugin,20);
    check(value(p,HeadphoneA)==1&&value(p,HeadphoneB)==0&&p.headphoneLeds[0]==127,"physical headphone Cue toggles and feeds back");
    float energy=0;for(float v:a.pcm[2])energy+=v*v;check(energy>0,"muted crossfaded-away deck remains audible in headphones");
    for(unsigned ch=0;ch<2;++ch)for(float v:a.pcm[ch])check(v==0,"cue never leaks onto master");
    a.midi(0xb4,0x43,127);a.run(plugin,40);check(value(p,HeadphoneMix)==1,"hardware cue mix mapped");
    for(float v:a.pcm[2])check(std::abs(v)<.000001,"master blend endpoint respects silent main faders");
    a.midi(0xb4,0x43,0);a.run(plugin,20);setValue(p,HeadphoneOutput,17);a.run(plugin,20);
    for(float v:a.pcm[2])check(v==0,"old cue pair clears when rerouted");energy=0;for(float v:a.pcm[30])energy+=v*v;check(energy>0,"cue moves to user-selected pair 31/32");
    setValue(p,HeadphoneOutput,3);setValue(p,Stems,1);a.run(plugin,20);
    for(unsigned ch=2;ch<4;++ch)for(float v:a.pcm[ch])check(v==0,"conflicting explicit cue cannot overwrite muted A stem");
    setValue(p,HeadphoneOutput,1);a.run(plugin,20);energy=0;for(float v:a.pcm[6])energy+=v*v;check(energy>0,"automatic cue moves after stereo stems");
    setValue(p,HeadphoneFold,2);setValue(p,HeadphoneLevel,-12);Bytes state;check(saveState(plugin,&state.out),"cue routing saves");
    check(loadState(plugin,&state.in)&&value(p,HeadphoneA)==1&&value(p,HeadphoneFold)==2&&value(p,HeadphoneLevel)==-12,"cue routing recalls");
    Bytes old;old.data=state.data;old.data[4]=8;
    check(loadState(plugin,&old.in)&&value(p,HeadphoneA)==0&&value(p,HeadphoneB)==0&&value(p,HeadphoneMix)==0&&value(p,HeadphoneLevel)==-6,"v8 reserved slots migrate to silent cue defaults");
    a.run(plugin); // commit the recalled document/stop before testing new starts
    std::array<float,2> positions{};
    for(unsigned cycle=0;cycle<2;++cycle){plugin->reset(plugin);setValue(p,param(0,Playback),1);setValue(p,param(0,SourceMode),4);
        setValue(p,param(0,StackNavigation),0);setValue(p,param(0,Clock),0);setValue(p,param(0,StackCycle),cycle?4:1);
        setValue(p,param(0,family(NeonFamily::StackShape)),1);setValue(p,param(0,Cycle),16);a.run(plugin);
        command(p,{CommandKind::Play,0});a.run(plugin,40);p.refreshLeds.store(true);a.run(plugin);
        positions[cycle]=p.stackPosition[0].load();const int expected=1+std::min(23,int(std::clamp(positions[cycle],0.f,1.f)*24));
        check(p.jogLeds[0][0]==expected&&p.jogLeds[0][2]==6,"Follow Source hardware needle uses live engine stack position");}
    check(positions[0]>positions[1]+.05f,"Stack Cycle seconds changes Follow Source ring travel independently of source cycle");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}

void headphoneBinaryWorkflow(const clap_plugin_t* plugin,const clap_plugin_params_t* params,Audio& audio){
    const auto set=[&](unsigned index,double value){clap_event_param_value_t e{};e.header={sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};e.param_id=index+1;e.value=value;
        clap_input_events_t input{&e,[](const clap_input_events_t*)->uint32_t{return 1;},[](const clap_input_events_t* in,uint32_t)->const clap_event_header_t*{return &static_cast<clap_event_param_value_t*>(in->ctx)->header;}};
        params->flush(plugin,&input,nullptr);};
    plugin->reset(plugin);set(Controller,1);set(Format,0);set(Stems,0);set(Crossfade,1);
    set(param(0,Level),0);set(param(0,Playback),0);set(param(0,Layer),0);set(param(0,StackNavigation),0);set(param(0,SourceMode),1);
    set(HeadphoneOutput,1);set(HeadphoneA,0);set(HeadphoneB,0);set(HeadphoneMix,0);set(HeadphoneLevel,-6);
    audio.midi(0x90,0x60,127);audio.midi(0x90,0x61,127);audio.midi(0x80,0x61,0);audio.run(plugin,40);
    float energy=0;for(float v:audio.pcm[2])energy+=v*v;check(energy>0,"exact installed candidate sends pre-fader cue to 3/4");
    for(unsigned ch=0;ch<2;++ch)for(float v:audio.pcm[ch])check(v==0,"exact binary cue remains separate from master");
    audio.midi(0xb4,0x43,127);audio.run(plugin,40);
    for(float v:audio.pcm[2])check(std::abs(v)<.000001,"exact binary hardware Cue Mix reaches main endpoint");
    set(HeadphoneA,0);set(HeadphoneMix,0);audio.run(plugin,40);
}
