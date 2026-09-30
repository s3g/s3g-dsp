// Audio-thread only. A blended stack can publish several heads, ordered by
// voice/layer storage, NOT by audibility. Keep a source address together and
// choose the strongest lane, then its strongest head. Work is fixed/bounded.
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
    return {true,static_cast<uint8_t>(best),std::clamp(double(cursors[heads[best]].sourcePositionNormalized),0.,1.)};
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
    return {bool(layer.asset),static_cast<uint8_t>(selected),std::clamp(first+(last-first)*control(p,d,Position),0.,1.)};
}
void selectCueLayer(Plugin& p,const Document& doc,unsigned d,unsigned layer) noexcept {
    const auto& source=doc.decks[d].layers[layer];
    setValue(p,param(d,Layer),layer);setValue(p,param(d,Start),source.start);setValue(p,param(d,End),source.end);
    setValue(p,param(d,SourceMode),1);setValue(p,param(d,StackNavigation),0);
    auto& perf=p.performance[d];perf.window=false;perf.riding=false;perf.rideOrder={};perf.rideFine=0;
}
