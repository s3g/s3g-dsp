// Audio-thread only. Keep the strongest lane's time address, plus a fractional
// stack address for techniques that blend. Work is fixed/bounded.
bool blendsStack(const Plugin& p,unsigned d) noexcept {
    const unsigned method=static_cast<unsigned>(control(p,d,Playback));
    return method!=0&&method!=3&&method!=8;
}
DeckBookmark renderedCue(const Plugin& p,const Document& doc,unsigned d) noexcept {
    std::array<float,32> energy{};
    std::array<int,32> heads;heads.fill(-1);
    const auto& cursors=p.engine.cursors(d);
    for(unsigned n=0;n<p.engine.cursorCount(d);++n){const auto& head=cursors[n];
        const unsigned layer=p.performance[d].window?p.performance[d].layer:head.layer;
        if(layer>=32||!head.sourceAsset||head.sourceAsset!=doc.decks[d].layers[layer].asset.get()
            ||!std::isfinite(head.sourcePositionNormalized)||head.sourcePositionNormalized<0||!std::isfinite(head.level))continue;
        energy[layer]+=head.level*head.level;
        if(heads[layer]<0||head.level>=cursors[heads[layer]].level)heads[layer]=static_cast<int>(n);
    }
    int best=-1;
    for(unsigned layer=0;layer<32;++layer)if(heads[layer]>=0&&(best<0||energy[layer]>energy[best]))best=static_cast<int>(layer);
    if(best<0)return {};
    DeckBookmark cue{true,static_cast<uint8_t>(best),std::clamp(double(cursors[heads[best]].sourcePositionNormalized),0.,1.)};
    const unsigned layers=doc.decks[d].stack.count;
    const bool navigating=control(p,d,StackNavigation)!=0||p.performance[d].riding||control(p,d,SourceMode)==4||control(p,d,Playback)==6;
    if(blendsStack(p,d)&&navigating&&!p.performance[d].window&&layers>1){
        cue.stackPosition=std::clamp(p.engine.stackPosition(d),0.f,1.f)*(layers-1);
        if(control(p,d,Playback)==6){
            // Lanes publishes an energy centroid, not the equal-power blend
            // coordinate. Invert the two audible lane gains to retain the mix.
            int peer=-1;
            for(unsigned n=0;n<32;++n)if(n!=unsigned(best)&&energy[n]>0&&(peer<0||energy[n]>energy[peer]))peer=int(n);
            if(peer<0)cue.stackPosition=float(best);
            else {const int lo=std::min(best,peer),hi=std::max(best,peer);
                const double mix=std::atan2(std::sqrt(energy[hi]),std::sqrt(energy[lo]))/1.5707963267948966;
                cue.stackPosition=static_cast<float>(lo+(hi-lo)*mix);}
        }
    }
    return cue;
}
DeckBookmark cueForStore(const Plugin& p,const Document& doc,unsigned d,
    const std::array<SampleNeonEvent,64>& events,unsigned count) noexcept {
    const unsigned selected=static_cast<unsigned>(control(p,d,Layer));
    const bool changed=p.lastLayer[d]!=selected||p.lastMethod[d]!=static_cast<unsigned>(control(p,d,Playback));
    const bool launching=std::any_of(events.begin(),events.begin()+count,[d](const auto& e){
        return e.slot==d&&e.kind==SampleNeonEventKind::Trigger;});
    // The published cursors belong to the preceding render. A fresh layer or
    // launch in this event batch must not borrow the previous source address.
    if(p.playing[d].load()&&!changed&&!launching){const auto cue=renderedCue(p,doc,d);if(cue.set)return cue;}
    const auto& layer=doc.decks[d].layers[selected];
    const double first=p.lastLayer[d]!=selected?layer.start:control(p,d,Start);
    const double last=p.lastLayer[d]!=selected?layer.end:control(p,d,End);
    DeckBookmark cue{bool(layer.asset),static_cast<uint8_t>(selected),std::clamp(first+(last-first)*control(p,d,Position),0.,1.)};
    if(blendsStack(p,d)&&!p.performance[d].window&&control(p,d,StackNavigation)!=0&&doc.decks[d].stack.count>1)
        cue.stackPosition=control(p,d,StackPosition)*(doc.decks[d].stack.count-1);
    return cue;
}
bool selectCueLayer(Plugin& p,const Document& doc,unsigned d,unsigned layer,float stackPosition=-1) noexcept {
    const auto& source=doc.decks[d].layers[layer];
    setValue(p,param(d,Layer),layer);setValue(p,param(d,Start),source.start);setValue(p,param(d,End),source.end);
    const bool blend=stackPosition>=0&&blendsStack(p,d)&&doc.decks[d].stack.count>1;
    if(blend)setValue(p,param(d,StackPosition),stackPosition/(doc.decks[d].stack.count-1));
    else setValue(p,param(d,SourceMode),1);
    setValue(p,param(d,StackNavigation),blend?1:0);
    auto& perf=p.performance[d];perf.window=false;perf.riding=false;perf.rideOrder={};perf.rideFine=0;
    return blend;
}
