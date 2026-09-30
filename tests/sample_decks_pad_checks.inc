void padModeWorkflow(Plugin& p,const clap_plugin_t* plugin,Audio& a){
    const auto baseline=[&](unsigned method){reset(plugin);setValue(p,Crossfade,0);setValue(p,param(0,Playback),method);
        setValue(p,param(0,Layer),0);setValue(p,param(0,SourceMode),1);setValue(p,param(0,Position),0);
        setValue(p,param(0,Start),0);setValue(p,param(0,End),1);setValue(p,param(0,PadBank),0);
        setValue(p,param(0,StackNavigation),0);setValue(p,param(0,SamplerPads),0);setValue(p,param(0,ScreenLatch),0);a.run(plugin);};
    const auto press=[&](unsigned mode,unsigned pad){setValue(p,param(0,PadMode),mode);command(p,{CommandKind::Pad,0,pad,.7});a.run(plugin);};
    const auto release=[&](unsigned pad){command(p,{CommandKind::Pad,0,pad,0});a.run(plugin);};
    baseline(0);press(0,0);check(!p.engine.active(0),"empty cue is inactive");
    setValue(p,param(0,Layer),2);setValue(p,param(0,Position),.37);a.run(plugin);
    command(p,{CommandKind::StoreBookmark,0,0});a.run(plugin);service(p);
    check(p.document->decks[0].bookmarks[0].set&&p.document->decks[0].bookmarks[0].layer==2
        &&std::abs(p.document->decks[0].bookmarks[0].position-.37)<1.e-6,"bookmark stores independent layer and position");
    baseline(1);press(0,0);check(control(p,0,Layer)==2&&std::abs(control(p,0,Position)-.37)<1.e-6&&control(p,0,Playback)==1,"cue recalls bookmark without replacing engine");
    release(0);check(p.engine.active(0),"cue release does not stop playback");
    for(unsigned method=0;method<9;++method){baseline(method);press(0,0);a.run(plugin,4);
        bool anchored=false;for(unsigned n=0;n<p.engine.cursorCount(0);++n){const auto& c=p.engine.cursors(0)[n];
            anchored|=c.sourceAsset==p.document->decks[0].layers[2].asset.get()&&c.sourcePositionNormalized>.20&&c.sourcePositionNormalized<.65;}
        if(!anchored){std::cerr<<"Bookmark missed in "<<kMethods[method]<<"; ";for(unsigned n=0;n<p.engine.cursorCount(0);++n)std::cerr<<p.engine.cursors(0)[n].sourcePositionNormalized<<"/L"<<unsigned(p.engine.cursors(0)[n].layer)<<" ";std::cerr<<'\n';}
        check(anchored,"every cue uses the bookmarked layer and source neighborhood");release(0);
    }
    command(p,{CommandKind::ClearBookmark,0,0});a.run(plugin);service(p);check(!p.document->decks[0].bookmarks[0].set,"clear bookmark");
    history(p,false);check(p.document->decks[0].bookmarks[0].set,"bookmark clear can be undone");
    for(unsigned method=0;method<9;++method){
        baseline(method);setValue(p,param(0,SliceHold),0);press(3,4);
        auto s=settings(p,*p.document);
        check(s.slots[0].playback==static_cast<SampleNeonPlayback>(method)&&s.slots[0].stack->count==1
            &&std::abs(s.slots[0].start-.5)<1.e-6&&std::abs(s.slots[0].end-.625)<1.e-6,"slice window belongs to selected method");
        check(a.run(plugin,8)>1.e-7,"slices sound through every method");
        for(unsigned i=0;i<p.engine.cursorCount(0);++i){const auto& c=p.engine.cursors(0)[i];
            if(c.sourcePositionNormalized<.495||c.sourcePositionNormalized>.63)std::cerr<<"Window escaped: "<<kMethods[method]<<" "<<c.sourcePositionNormalized<<'\n';
            check(c.sourcePositionNormalized>=.495&&c.sourcePositionNormalized<=.63,"actual slice read heads stay in the window");}
        setValue(p,param(0,PadMode),2);setValue(p,param(0,PadBank),3);release(4);
        check(p.engine.active(0),"one-shot survives pad release and page/bank change");a.run(plugin,180);
        check(!p.engine.active(0),"every one-shot ends, including Cutups and Stretch");
        baseline(method);setValue(p,param(0,SliceHold),1);press(3,4);a.run(plugin,80);
        check(p.engine.active(0),"held slice runs past source duration");
        setValue(p,param(0,PadBank),1);setValue(p,param(0,PadMode),6);release(4);a.run(plugin,100);
        check(!p.engine.active(0),"held slice release remains owned after bank/page switch");
        baseline(method);setValue(p,param(0,PadBank),1);press(3,0);check(!p.engine.active(0),"missing slice is inactive, not last-slice clamp");
        baseline(method);command(p,{CommandKind::Play,0});a.run(plugin,10);press(1,1);release(1);a.run(plugin,20);
        check(p.performance[0].window&&p.performance[0].loop&&p.engine.active(0),"source loop is toggled, not held");
        check(control(p,0,Playback)==method&&control(p,0,Start)==0&&control(p,0,End)==1,"loops preserve method and saved trim");
        press(1,1);release(1);check(!p.performance[0].window&&p.engine.active(0),"press active loop exits to full source");
    }
    baseline(2);setValue(p,param(0,SliceRaw),1);press(3,2);check(settings(p,*p.document).slots[0].playback==SampleNeonPlayback::Sample&&control(p,0,Playback)==2,"RAW is explicit and temporary");release(2);setValue(p,param(0,SliceRaw),0);
    baseline(0);setValue(p,param(0,Character),1);setValue(p,param(0,Amount),.21);setValue(p,param(0,FxLatch),0);
    press(2,4);check(settings(p,*p.document).slots[0].character==SampleNeonMangleCharacter(4)&&control(p,0,Character)==1,"FX punch never overwrites saved effect");
    press(2,6);command(p,{CommandKind::PadPressure,0,6,.25});a.run(plugin);check(std::abs(settings(p,*p.document).slots[0].mangle-.25)<1.e-6,"FX aftertouch follows downward pressure");
    setValue(p,param(0,PadMode),3);release(6);check(settings(p,*p.document).slots[0].character==SampleNeonMangleCharacter(4),"overlapping FX release restores previous held effect");
    release(4);check(settings(p,*p.document).slots[0].character==SampleNeonMangleCharacter(1)&&std::abs(control(p,0,Amount)-.21)<1.e-6,"FX release restores base settings");
    setValue(p,param(0,FxLatch),1);press(2,3);release(3);check(newest(p.performance[0].fxOrder)==3,"FX toggle survives release");press(2,3);release(3);check(newest(p.performance[0].fxOrder)<0,"FX toggle off");setValue(p,param(0,FxLatch),0);setValue(p,param(0,Amount),0);
    for(unsigned method=0;method<9;++method){baseline(method);command(p,{CommandKind::Play,0});a.run(plugin,40);
        const auto note=p.engine.cursors(0)[0].noteId;press(6,2);a.run(plugin,130);
        check(control(p,0,Layer)==0&&control(p,0,StackNavigation)==0,"stack ride preserves edit selection/navigation");
        bool found=false;for(unsigned block=0;block<120&&!found;++block){a.run(plugin);for(unsigned n=0;n<p.engine.cursorCount(0);++n)found|=p.engine.cursors(0)[n].sourceAsset==p.document->decks[0].layers[2].asset.get();}
        if(!found){std::cerr<<"Ride target missing in "<<kMethods[method]<<"; cursors "<<p.engine.cursorCount(0)<<" target "<<performanceStackTarget(p,0)<<'\n';}
        check(found,"stack ride reaches target layer in every method");
        if(method==0)check(p.engine.cursors(0)[0].noteId==note,"Sample stack ride preserves voice identity");
        setValue(p,param(0,PadMode),4);release(2);a.run(plugin,150);check(p.engine.active(0)&&performanceStackTarget(p,0)==-1,"stack ride release resumes saved navigation");
    }
    baseline(0);command(p,{CommandKind::Play,0});a.run(plugin,50);const double before=currentPosition(p,0);press(5,3);a.run(plugin,30);
    check(p.engine.roll(0).active()&&currentPosition(p,0)>before+.1,"ROLL repeats rendered audio while live source continues");
    setValue(p,param(0,PadMode),0);release(3);a.run(plugin,2);check(!p.engine.roll(0).active()&&p.engine.active(0),"ROLL release returns to live engine after page switch");
    baseline(0);setValue(p,param(0,SliceHold),0);setValue(p,param(0,PadMode),4);
}

void capturePadWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"capture pad plugin");auto& p=*self(plugin);loadFixture(p);
    check(plugin->activate(plugin,48000,1,256),"capture pad activation");plugin->start_processing(plugin);Audio a;setValue(p,Crossfade,0);a.run(plugin);
    command(p,{CommandKind::Play,0});a.run(plugin,4);
    setValue(p,param(0,PadMode),7);command(p,{CommandKind::Pad,0,0,1});a.run(plugin);service(p);
    check(p.capture.state.load()==1&&p.capture.capacity==24000,"one-beat capture is exactly half a second at 120 BPM");
    command(p,{CommandKind::Pad,0,0,0});a.run(plugin,96);check(p.capture.state.load()==3,"timed capture stops without a held button");service(p);waitWork(p);
    check(p.lastTake[0].load()==35&&p.document->decks[1].layers[3].asset->frameCount()==24000,"timed capture appends on receiver");a.run(plugin);
    const float fader=value(p,Crossfade);command(p,{CommandKind::Pad,0,6,1});a.run(plugin,4);
    check(p.engine.active(1)&&control(p,1,Layer)==3&&value(p,Crossfade)==fader,"audition targets latest receiver without moving crossfader");
    setValue(p,param(0,PadMode),2);command(p,{CommandKind::Pad,0,6,0});a.run(plugin,80);check(!p.engine.active(1),"audition release belongs to capture page");
    setValue(p,param(0,PadMode),7);command(p,{CommandKind::Pad,0,7,1});a.run(plugin,4);command(p,{CommandKind::Pad,0,7,0});a.run(plugin,4);check(p.engine.active(1),"launch continues after release");
    command(p,{CommandKind::Pad,0,4,1});a.run(plugin);service(p);command(p,{CommandKind::Pad,0,4,0});a.run(plugin,4);
    command(p,{CommandKind::Pad,0,5,1});a.run(plugin);service(p);command(p,{CommandKind::Pad,0,5,0});a.run(plugin);service(p);waitWork(p);
    check(p.document->decks[1].layers[4].asset&&p.capture.state.load()==1&&p.capture.target==5,"Take commits previous capture and arms next free layer");
    a.run(plugin,4);command(p,{CommandKind::Pad,0,4,1});a.run(plugin);service(p);a.run(plugin);service(p);waitWork(p);
    check(p.capture.state.load()==0&&p.document->decks[1].layers[5].asset,"Record pad toggles capture off");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}

void rollFieldWorkflow(){
    DeckRoll roll;roll.prepare(48000);std::array<std::array<float,64>,16> buffer{};std::array<float*,16> ptr{};
    for(unsigned ch=0;ch<16;++ch)ptr[ch]=buffer[ch].data();
    for(unsigned width:{2u,4u,8u,16u}){roll.reset();unsigned time=0;
        const auto render=[&](unsigned blocks){for(unsigned b=0;b<blocks;++b){for(unsigned ch=0;ch<width;++ch)for(unsigned n=0;n<64;++n)buffer[ch][n]=std::sin((time+n)*.02f)*(ch%2?-1:1);
            roll.process(ptr.data(),width,64);time+=64;
            for(unsigned ch=1;ch<width;++ch)for(unsigned n=0;n<64;++n)check(std::abs(buffer[ch][n]-(ch%2?-buffer[0][n]:buffer[0][n]))<1.e-6,"roll entry/repeat/release preserves spatial channel coherence");}};
        check(!roll.start(.01),"no rendered history means no roll");render(32);check(roll.start(.02),"freeze recent output history");
        const unsigned before=roll.historyFrames();render(48);check(roll.historyFrames()>before,"hidden live audio fills next history while holding roll");
        roll.release();render(12);check(!roll.active(),"roll exits");check(!roll.start(4.1),"roll never exceeds bounded four-second history");
    }
}
