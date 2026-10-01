void stackCueWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"stack cue plugin");
    auto& p=*self(plugin);loadFixture(p);check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"stack cue activation");Audio a;
    for(unsigned method:{1u,2u,4u,5u,6u,7u}){
        reset(plugin);setValue(p,param(0,Playback),method);setValue(p,param(0,Layer),0);
        setValue(p,param(0,SourceMode),4);setValue(p,param(0,StackNavigation),1);setValue(p,param(0,StackPosition),.35);
        setValue(p,param(0,PadMode),0);setValue(p,param(0,Position),.3);setValue(p,param(0,Spray),0);a.run(plugin);
        command(p,{CommandKind::Play,0});a.run(plugin,100);
        const float audible=p.engine.stackPosition(0);
        command(p,{CommandKind::StoreBookmark,0,0});command(p,{CommandKind::SetCue,0});a.run(plugin);service(p);
        const auto cue=p.document->decks[0].bookmarks[0];
        check(std::abs(cue.stackPosition-.7)<.003,"cue preserves fractional lane address, including Lanes equal-power inversion");
        check(std::abs(control(p,0,CueStackPosition)-cue.stackPosition)<1.e-6,"main and pad cues store the same blend");
        for(bool main:{false,true}){
            setValue(p,param(0,Layer),2);setValue(p,param(0,StackPosition),.9);a.run(plugin,80);
            command(p,{main?CommandKind::Cue:CommandKind::Pad,0,0,1});a.run(plugin,4);
            check(control(p,0,StackNavigation)==1&&std::abs(control(p,0,StackPosition)-.35)<.002,"recall holds the saved blend instead of its dominant layer");
            check(std::abs(p.engine.stackPosition(0)-audible)<.004,"rendered blend recalls without a sweep from layer zero");
            check(control(p,0,SourceMode)==4,"blend cue does not overwrite configured Stack Path");
            command(p,{CommandKind::Pad,0,0,0});a.run(plugin);
        }
        const auto notes=p.engine.noteCount(0);const auto rate=control(p,0,Rate),position=control(p,0,Position);
        a.midi(0x90,0x1d,127);a.run(plugin,100);
        check(control(p,0,StackNavigation)==0&&p.lastStackTarget[0]==-1&&p.engine.noteCount(0)==notes,"Shift Sync resumes source without retriggering voices");
        check(control(p,0,Rate)==rate&&control(p,0,Position)==position,"Shift Sync does not change speed or source position");
    }
    // Main and pad blends survive save/load; old cues remain single-layer.
    Bytes state;check(saveState(plugin,&state.out),"save blend cues");
    check(loadState(plugin,&state.in)&&std::abs(p.document->decks[0].bookmarks[0].stackPosition-.7)<.003
        &&std::abs(control(p,0,CueStackPosition)-.7)<.003,"blend cues survive project recall");
    Bytes old;versionSeven(state,old);check(loadState(plugin,&old.in)&&p.document->decks[0].bookmarks[0].stackPosition==-1
        &&control(p,0,CueStackPosition)==-1,"v7 cues migrate without inventing fractional positions");
    Bytes invalid;invalid.data=state.data;const float bad=32;std::memcpy(invalid.data.data()+invalid.data.size()-64,&bad,4);
    check(!loadState(plugin,&invalid.in)&&p.document->decks[0].bookmarks[0].stackPosition==-1,"invalid blend address rejects transactionally");
    state.at=0;check(loadState(plugin,&state.in),"restore blend cues");a.run(plugin);
    auto doc=std::make_shared<Document>(*p.document);doc->decks[0].layers[8]=doc->decks[0].layers[0];publish(p,std::move(doc),1);a.run(plugin);
    command(p,{CommandKind::Cue,0});a.run(plugin,4);
    check(std::abs(control(p,0,StackPosition)-.7/8)<.001,"appending stack layers does not move a saved fractional lane address");
    // Both hardware encodings and the independent right deck are supported.
    setValue(p,param(1,StackNavigation),1);setValue(p,param(0,StackNavigation),1);
    a.midi(0x91,0x1c,127);a.midi(0x91,0x5d,127);a.run(plugin);a.midi(0x81,0x1c,0);a.run(plugin);
    check(control(p,1,StackNavigation)==0&&control(p,0,StackNavigation)==1,"held Shift plus normal Sync targets only its own deck");
    setValue(p,param(1,Rate),.7);setValue(p,param(1,Position),.6);a.midi(0x91,0x5d,127);a.run(plugin);
    check(control(p,1,Position)==0&&control(p,1,Rate)==1,"ordinary Sync retains rate-match and restart behavior");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
}

void layerClipboardWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"clipboard plugin");auto& p=*self(plugin);loadFixture(p);
    auto doc=std::make_shared<Document>(*p.document);doc->decks[0].layers[1].start=.2;doc->decks[0].layers[1].end=.7;
    doc->decks[0].layers[1].slices=equalSampleNeonSliceLayout(13);publish(p,std::move(doc),1,false);
    const auto source=p.document->decks[0].layers[1];const auto undo=p.undo.size();
    check(!editStackLayer(p,1,7,LayerEdit::Paste)&&!editStackLayer(p,1,7,LayerEdit::Copy),"empty clipboard and empty layer copy are no-ops");
    check(editStackLayer(p,0,1,LayerEdit::Copy)&&p.undo.size()==undo,"copy does not mutate undo history");
    setValue(p,param(1,Layer),7);
    check(editStackLayer(p,1,7,LayerEdit::Paste),"cross-deck paste into empty slot");
    const auto pasted=p.document->decks[1].layers[7];
    check(pasted.asset==source.asset&&pasted.waveform==source.waveform&&pasted.wavesets==source.wavesets
        &&pasted.slices.sliceCount==13&&pasted.start==.2&&pasted.end==.7,"clipboard shares immutable audio/analysis and preserves trim and slices");
    check(std::abs(control(p,1,Start)-.2)<1.e-6&&std::abs(control(p,1,End)-.7)<1.e-6,"selected layer controls follow pasted trim");
    history(p,false);check(!p.document->decks[1].layers[7].asset,"paste undo restores empty destination");
    history(p,true);check(p.document->decks[1].layers[7].asset==source.asset,"paste redo restores exact asset");
    check(editStackLayer(p,0,1,LayerEdit::Clear)&&!p.document->decks[0].layers[1].asset,"clear only the requested slot");
    check(p.document->decks[1].layers[7].asset==source.asset,"clearing original does not clear shared copy");
    history(p,false);check(p.document->decks[0].layers[1].asset==source.asset,"clear undo retains original audio");
    p.capture.state.store(1);check(!editStackLayer(p,1,7,LayerEdit::Clear),"recording blocks destructive layer edits");p.capture.state.store(0);
    Bytes copied;check(saveState(plugin,&copied.out),"save copied layer stack");
    // Paste still more aliases: saved state grows by metadata, not source PCM.
    for(unsigned n=8;n<32;++n)check(editStackLayer(p,1,n,LayerEdit::Paste),"fill stack with shared clipboard audio");
    Bytes filled;check(saveState(plugin,&filled.out)&&filled.data.size()<copied.data.size()+24000,
        "copied layers do not multiply embedded audio costs");
    check(loadState(plugin,&filled.in),"recall shared clipboard audio");
    const auto& restored=p.document->decks[0].layers[1];
    for(unsigned n=7;n<32;++n){const auto& layer=p.document->decks[1].layers[n];
        check(layer.asset==restored.asset&&layer.waveform==restored.waveform&&layer.wavesets==restored.wavesets
            &&layer.start==.2&&layer.end==.7&&layer.slices.sliceCount==13,"recall keeps shared audio/analysis and per-layer edits");}
    Bytes again;check(saveState(plugin,&again.out)&&again.data==filled.data,"shared-audio state roundtrips exactly");
    plugin->destroy(plugin);
}

void jogLedWorkflow(){
    const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"jog LEDs plugin");auto& p=*self(plugin);loadFixture(p);
    Audio a;p.hazard.store(p.document.get());
    const auto feedback=[&]{a.feedbackCount=0;beatpadFeedback(p,&a.out,4800);};
    feedback();check(a.feedbackCount==270,"jog rings included in first synchronization");
    for(unsigned d=0;d<2;++d)for(unsigned n=0;n<6;++n){const auto& e=a.feedback[256+d*6+n];
        check(e.data[0]==0x90+d&&e.data[1]==0x67+n&&e.data[2]==p.jogLeds[d][n],"ring uses per-deck manufacturer note addresses");}
    feedback();check(a.feedbackCount==0,"stationary rings send no duplicate MIDI");
    setValue(p,param(0,StackNavigation),1);setValue(p,param(0,StackPosition),.5);feedback();
    check(p.jogLeds[0][0]==13&&p.jogLeds[0][2]==4&&p.jogLeds[1][2]==6,"manual stack midpoint is yellow; other deck stays cyan");
    feedback();check(!a.feedbackCount,"stationary manual stack also stays quiet");
    p.capture.to=1;p.capture.state.store(2);feedback();check(p.jogLeds[1][2]==1&&p.jogLeds[0][2]==4,"receiving deck recording color takes priority");
    p.capture.state.store(0);p.shift[1]=true;feedback();check(p.jogLeds[1][2]==4,"Shift temporarily displays stack feedback");
    p.shift[1]=false;p.playing[1].store(true);setValue(p,param(1,Playback),1);p.scanPosition[1].store(.8);p.stackPosition[1].store(.25);feedback();
    check(p.jogLeds[1][0]==7&&p.jogLeds[1][2]==6,"Follow Source ring shows live stack position, not the independent source scan cycle");
    p.scanPosition[1].store(.1);feedback();check(!a.feedbackCount,"source-time rotation cannot move a stationary stack ring");
    p.stackPosition[1].store(.75);feedback();check(p.jogLeds[1][0]==19,"Follow Source ring follows Stack Cycle movement");
    a.feedbackLimit=0;p.refreshLeds.store(true);feedback();check(p.jogLeds[0][0]==-1,"rejected jog messages stay dirty");
    a.feedbackLimit=512;feedback();check(a.feedbackCount==270,"all rejected pad and jog messages retry");
    p.hazard.store(nullptr);plugin->destroy(plugin);
}
