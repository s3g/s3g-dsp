// CLAP reset has no reason code. REAPER sends it before Record starts, even
// for a Free-clock deck already playing while the host is stopped. Remember
// only deliberate latched transport intent; reset still clears every voice
// and DSP buffer. A fresh launch is permitted ONLY on the very next process
// call, if that call establishes a stopped -> playing host transition.
void rememberTransportForHostStart(Plugin& p) noexcept {
    for(unsigned d=0;d<2;++d){auto& r=p.transportRecovery[d];r.pending=false;
        if(!p.hostTransportKnown||p.hostPlaying||!p.transportLatched[d]
            ||!p.engine.active(d)||control(p,d,Clock)!=0)continue;
        r.position=p.engine.transportPosition(d);r.performance={};r.format=value(p,Format);
        if(p.performance[d].window&&p.performance[d].loop){
            const auto& w=p.performance[d];auto& s=r.performance;
            s.window=true;s.loop=true;s.pad=w.pad;s.bank=w.bank;s.layer=w.layer;
            s.start=w.start;s.end=w.end;s.note=w.note;
        }
        for(unsigned key=0;key<kDeckStride;++key)r.controls[key]=control(p,d,key);
        r.pending=true;
    }
}
void recoverTransportAtHostStart(Plugin& p,std::array<SampleNeonEvent,64>& events,unsigned& count) noexcept {
    for(unsigned d=0;d<2;++d){auto& r=p.transportRecovery[d];const bool pending=r.pending;r.pending=false;
        if(!pending||!p.hostTransportKnown||!p.hostPlaying||count==events.size())continue;
        bool unchanged=r.format==value(p,Format);
        for(unsigned key=0;key<kDeckStride;++key)unchanged&=r.controls[key]==control(p,d,key);
        if(!unchanged)continue;
        p.performance[d]=r.performance;
        auto& e=events[count++];e={};e.slot=d;e.noteId=r.performance.window?r.performance.note:d+1;
        e.value=r.position.velocity;e.sourceLayer=r.position.layer;e.selectedSource=r.position.selectedSource;
        e.transportPosition=&r.position;
        p.transportLatched[d]=true;p.playing[d].store(true);p.engine.fadeTransportIn(d);
    }
}
