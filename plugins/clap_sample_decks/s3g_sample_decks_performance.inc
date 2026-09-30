// Audio-thread gesture state is separate from saved parameters. Main/UI reads
// only the document and atomic publications, never this mutable state.
int newest(const std::array<uint64_t,8>& order) noexcept {
    int result=-1;for(unsigned n=0;n<8;++n)if(order[n]&&(result<0||order[n]>order[result]))result=static_cast<int>(n);return result;
}
bool padAvailable(const Plugin& p,const Document& doc,unsigned d,unsigned mode,unsigned bank,unsigned pad) noexcept {
    const auto& deck=doc.decks[d];const unsigned layer=static_cast<unsigned>(control(p,d,Layer));
    if(mode==0)return control(p,d,CueAction)==1?bool(deck.layers[layer].asset):deck.bookmarks[pad].set&&bool(deck.layers[deck.bookmarks[pad].layer].asset);
    if(mode==3)return deck.layers[layer].asset&&bank*8+pad<deck.layers[layer].slices.sliceCount;
    if(mode==4&&control(p,d,SamplerPads)!=0){const int key=keyboardNote(p,d,pad);return deck.layers[layer].asset&&key>=0&&key<128;}
    if(mode==4)return bool(deck.layers[bank*8+pad].asset);
    if(mode==6)return bool(deck.playbackStack(layer).layers[bank*8+pad].asset);
    if(mode==2)return deck.base()&&!(value(p,Format)>=3&&pad>=6);
    if(mode==5)return deck.base()&&std::ldexp(1.,int(pad)-5)*60./p.recordTempo.load()*p.sampleRate.load()<=p.rollHistory[d].load();
    if(mode==7){if(pad>=6){const int take=p.lastTake[d].load();return take>=0&&bool(doc.decks[take/32].layers[take%32].asset);}
        const unsigned to=value(p,RecordSource)!=0?d:1-d;
        const unsigned state=p.capture.state.load();if(state)return (state==1||state==2)&&p.capture.from==d;
        return doc.decks[to].firstEmpty()<32&&(value(p,RecordSource)!=0||deck.base());}
    return bool(deck.layers[layer].asset);
}
void performanceSettings(Plugin& p,const Document& doc,unsigned d,SampleNeonSlotSettings& c,unsigned frames) noexcept {
    auto& perf=p.performance[d];
    const int fx=newest(perf.fxOrder);
    if(fx>=0&&!(value(p,Format)>=3&&fx>=6)){
        c.character=static_cast<SampleNeonMangleCharacter>(fx);
        const float held=p.padHeld[d][fx]?std::max(p.padPressure[d][fx],p.screenHeld[d][fx]?control(p,d,ScreenPressure):0.f):0;
        if(held>0)perf.fxPressure[fx]=held;
        c.mangle=std::clamp(std::max(control(p,d,Amount),perf.fxPressure[fx]),0.f,1.f);c.pressureDepth=0;
        for(unsigned n=0;n<kNeonCharacterControls;++n)c.fx[n]=control(p,d,kFx+fx*kNeonCharacterControls+n);
    }
    const int ride=newest(perf.rideOrder);
    if(ride>=0)c.family[NeonFamily::LaneSlew]=control(p,d,RideTime);
    const unsigned layers=doc.decks[d].stack.count;
    if(perf.riding&&layers&&(c.playback==SampleNeonPlayback::Sample||c.playback==SampleNeonPlayback::SliceSequence||c.playback==SampleNeonPlayback::Cutups)){
        const float target=ride>=0?std::clamp(perf.rideTarget[ride]+perf.rideFine,0.f,1.f):layers>1?float(perf.rideReturnLayer)/(layers-1):0;
        // The continuous methods smooth on their own audio clock. These three
        // choose discrete layers on the same running voice/sequence clock.
        perf.ridePosition+=(target-perf.ridePosition)*static_cast<float>(1-std::exp(-double(frames)/(p.sampleRate.load()*control(p,d,RideTime))));
        unsigned layer=std::min(layers-1,static_cast<unsigned>(std::lround(perf.ridePosition*(layers-1))));
        if(!doc.decks[d].layers[layer].asset){layer=ride>=0?static_cast<unsigned>(std::lround(target*(layers-1))):perf.rideReturnLayer;}
        c.deckPerformanceLayer=static_cast<int>(layer);const auto& l=doc.decks[d].layers[layer];
        c.selectedLayer=layer;c.start=l.start;c.end=l.end;c.sliceLayout=l.slices;c.sliceCount=l.slices.sliceCount;
        if(c.playback==SampleNeonPlayback::Cutups){c.family[NeonFamily::CutFileOrder]=5;for(unsigned n=0;n<kMaximumCutupsPatternSteps;++n)c.family.values[neonFamilyIndex(NeonFamily::CutPatternLane)+n]=layer;}
        if(ride<0&&std::abs(target-perf.ridePosition)<.001f)perf.riding=false;
    }else {if(ride<0)perf.riding=false;
        if(c.playback==SampleNeonPlayback::SliceSequence&&c.sourceMode==NeonSourceMode::Selected)c.deckPerformanceLayer=c.selectedLayer;}
    if(!perf.window)return;
    const auto& source=doc.decks[d].layers[perf.layer];if(!source.asset)return;
    // A window is one source object shared by every method, not a legacy raw
    // HotLoop/Slicer event. No PCM copy or analysis occurs on the audio thread.
    auto& stack=p.performanceStacks[d];stack.count=1;stack.fragmentCount=1;
    stack.layers[0]={source.asset.get(),perf.start,perf.end,source.wavesets.get()};
    stack.cutupsMetadata[0]=source.analysis;stack.sourceBpms[0]=control(p,d,SourceBpm);
    stack.primarySliceCount=1;stack.primarySlices={};stack.primarySlices[1]=1;
    stack.fragments[0]={static_cast<float>(perf.start),static_cast<float>(perf.end),0,0,0};
    c.stack=&stack;c.selectedLayer=0;c.deckPerformanceLayer=0;c.sourceMode=NeonSourceMode::Selected;c.start=perf.start;c.end=perf.end;
    c.sourceDurationSeconds=source.asset->frameCount()/source.asset->sampleRate;
    c.sliceLayout=equalSampleNeonSliceLayout(1);c.sliceCount=1;
    c.family[NeonFamily::MosaicScope]=0;c.family[NeonFamily::CutFileOrder]=0;c.family[NeonFamily::LaneAuto]=0;
    c.launchPosition=0;c.grainPosition=0;c.triggerMode=TriggerMode::Gate;c.repeat=perf.loop||perf.heldSlice;
    if(perf.raw)c.playback=SampleNeonPlayback::Sample;
    const float duration=static_cast<float>((perf.end-perf.start)*c.sourceDurationSeconds/control(p,d,Rate));
    c.techniqueAttackSeconds=std::min(c.techniqueAttackSeconds,duration*.1f);
    c.techniqueReleaseSeconds=std::min(c.techniqueReleaseSeconds,duration*.2f);
}
float performanceStackTarget(const Plugin& p,unsigned d) noexcept {
    const auto& perf=p.performance[d];if(perf.window)return -1;
    const int ride=newest(perf.rideOrder);
    return ride>=0?std::clamp(perf.rideTarget[ride]+perf.rideFine,0.f,1.f):control(p,d,StackNavigation)!=0?control(p,d,StackPosition):-1.f;
}
