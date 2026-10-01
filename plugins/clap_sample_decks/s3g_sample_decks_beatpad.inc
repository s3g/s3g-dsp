// Beatpad 2 adapter. Protocol addresses and LED color numbers stay here;
// DSP/editor actions and semantic pad feedback are device-independent.
unsigned beatpadMacro(const Plugin& p,unsigned d) noexcept {
    switch(static_cast<unsigned>(control(p,d,Playback))){
        case 1:return family(NeonFamily::MotionWindow);
        case 2:return GrainSize;
        case 3:return Technique2; // repeats work in both Free and Host clocks
        case 5:return control(p,d,family(NeonFamily::WavesetEngine))!=0?family(NeonFamily::OscGroup):Technique0;
        case 6:return family(NeonFamily::LaneSlew);
        case 7:return family(NeonFamily::SpectralBlur);
        case 8:return family(NeonFamily::CutRepeat);
        default:return Tune; // Sample / Stretch
    }
}
// One pickup state per physical knob, so moving a Shift function also updates
// the physical position used when returning to its ordinary mixer function.
void beatpadKnob(Plugin& p,unsigned d,unsigned key,unsigned raw) noexcept {
    const bool shifted=key<0x40||p.shift[d];const unsigned base=key|0x40u;
    if(base==0x49)p.beatpad.eqLayout(true);
    if(base==0x4d)p.beatpad.eqLayout(false);
    const unsigned knob=base==0x4b?0:base==0x4c?1:base==0x4d?2:base==0x49?3:p.beatpad.filterEq?2:3;
    unsigned target=base==0x4b?Gain:base==0x49?Filter:High+base-0x4c;
    if(shifted){const bool host=control(p,d,Clock)!=0;
        target=knob==0?(host?StackBeats:StackCycle):knob==1?(host?CycleBeats:Cycle):knob==2?beatpadMacro(p,d):Amount;}
    const unsigned index=param(d,target);const auto& def=p.defs[index];const double current=value(p,index);
    const bool db=!shifted&&base!=0x49;
    const bool logarithmic=shifted&&(target==StackCycle||target==StackBeats||target==Cycle||target==CycleBeats
        ||target==GrainSize||target==family(NeonFamily::MotionWindow)||target==family(NeonFamily::LaneSlew));
    double goal=db?(current<=0?64+current*64/24:64+current*63/12):!shifted?(current+1)*64:
        logarithmic?127*std::log(std::clamp(current,def.lo,def.hi)/def.lo)/std::log(def.hi/def.lo):127*(current-def.lo)/(def.hi-def.lo);
    goal=std::clamp(goal,0.,127.);
    auto& pickup=p.beatpad.knobs[d][knob];const int previous=pickup.position;pickup.position=static_cast<int>(raw);
    if(pickup.binding!=index){pickup.binding=index;pickup.caught=previous<0&&!shifted;}
    else if(std::abs(current-pickup.written)>1.e-5*(1+std::abs(current)))pickup.caught=false;
    const bool crossed=previous>=0&&(previous-goal)*(double(raw)-goal)<=0;
    if(!pickup.caught&&(std::abs(double(raw)-goal)<=1.5||crossed))pickup.caught=true;
    if(!pickup.caught)return;
    const double mapped=db?(raw<=64?(double(raw)-64)*24/64:(double(raw)-64)*12/63):!shifted?(double(raw)-64)/64:
        logarithmic?def.lo*std::pow(def.hi/def.lo,raw/127.):def.lo+(def.hi-def.lo)*raw/127.;
    setValue(p,index,mapped);pickup.written=value(p,index);
}
void beatpadMidi(Plugin& p,const uint8_t* bytes,std::array<SampleNeonEvent,64>& events,unsigned& count) noexcept {
    const unsigned kind=bytes[0]&0xf0u,ch=bytes[0]&15u,key=bytes[1]&127u,v=bytes[2]&127u;
    const bool on=kind==0x90&&v!=0,off=kind==0x80||(kind==0x90&&v==0);
    if((ch==2||ch==3)&&(kind==0x90||kind==0x80||kind==0xb0||kind==0xa0)) {
        const unsigned d=ch-2,unshifted=key&63u,mode=unshifted/8,pad=unshifted%8;
        if(kind==0xb0&&unshifted==0&&control(p,d,PadMode)==6){
            // Jog FX uses CC 00/40; velocity for these pads uses CC 30..37.
            audioCommand(p,{CommandKind::StackJog,d,0,double(int(v)-64)},events,count);return;
        }
        if(kind==0xb0||kind==0xa0){
            // Never treat velocity/aftertouch CCs as transport or notes. The
            // hardware also works with its fixed-velocity mode enabled.
            if(kind==0xa0)audioCommand(p,{CommandKind::PadPressure,d,pad,v/127.},events,count);
            else p.padVelocity[d][pad]=v/127.f;
            auto& pending=p.pendingPads[d][pad];
            if(kind==0xb0&&pending.active&&pending.mode==mode&&v){pending.velocity=v/127.f;commitPad(p,d,pad,events,count);}return;
        }
        // Capture the modifier at note-on. The release may use an unshifted
        // address after Shift/page changes; it must not jump or release a voice.
        if(off&&p.beatpad.storingCue[d][pad]){p.beatpad.storingCue[d][pad]=false;return;}
        if(on&&mode==0&&((key&64u)||p.shift[d])){
            if(!p.beatpad.storingCue[d][pad]){
                audioCommand(p,{CommandKind::StoreBookmark,d,pad},events,count);
                p.beatpad.storingCue[d][pad]=true;
                p.padOwner[d][pad]=0;p.padOwnerBank[d][pad]=0;p.padOwnerKeyboard[d][pad]=false;
                p.padVelocity[d][pad]=1;p.padPulse[d][pad]=static_cast<unsigned>(p.sampleRate.load()*.12);
            }return;
        }
        if(on){auto& pending=p.pendingPads[d][pad];pending={true,mode,static_cast<unsigned>(p.sampleRate.load()*.002),v/127.f};}
        else {if(p.pendingPads[d][pad].active)commitPad(p,d,pad,events,count);audioCommand(p,{CommandKind::Pad,d,pad,0},events,count);}return;
    }
    if(ch<2){const unsigned d=ch;
        if((on||off)&&key==0x1c){p.shift[d]=on;
            // Changing gesture mid-touch must not leave the time platter held.
            if(on&&p.touching[d].load()){audioCommand(p,{CommandKind::Touch,d,0,0},events,count);p.stackTouch[d]=true;}return;}
        if(on&&key>=0x48&&key<=0x4f){setValue(p,param(d,PadMode),key-0x48);p.refreshLeds.store(true);return;}
        if(on&&key==0x0c){setValue(p,param(d,SamplerPads),1-control(p,d,SamplerPads));setValue(p,param(d,PadMode),4);p.refreshLeds.store(true);return;}
        if(on&&key>=8&&key<=15)return; // Shift + Hot Cue remains the firmware velocity toggle.
        if((on||off)&&(key==0x41||key==0x42||key==0x01||key==0x02)){
            const unsigned button=(key&63u)-1;
            // Always release by physical button, even if Shift changed meanwhile.
            if(off)audioCommand(p,{CommandKind::Bend,d,button,0},events,count);
            else if(key<0x40||p.shift[d]){if(keyboardPads(p,d))setValue(p,param(d,KeyboardOctave),control(p,d,KeyboardOctave)+(button?1:-1));}
            else audioCommand(p,{CommandKind::Bend,d,button,1},events,count);
            return;
        }
        if((on||off)&&(key==0x63||key==0x23||key==0x64||key==0x24)){
            if(on&&(p.shift[d]||key<0x40)){p.stackTouch[d]=true;return;}
            if(off&&p.stackTouch[d]){p.stackTouch[d]=false;return;}
            audioCommand(p,{CommandKind::Touch,d,0,on?1.:0.},events,count);return;}
        if(on){switch(key){
            case 0x60:
                // Shift+fader-up emits PLAY, not Shift+Play (0x20). Treat it
                // as idempotent start-from-cue, never a play/stop toggle.
                if(p.shift[d]){p.beatpad.faderStart[d]=true;if(!p.playing[d].load())audioCommand(p,{CommandKind::Cue,d},events,count);}
                else audioCommand(p,{CommandKind::Play,d},events,count);return;
            case 0x73:case 0x33:
                if(p.shift[d]||p.beatpad.faderStart[d]){p.beatpad.faderStart[d]=false;audioCommand(p,{CommandKind::CueStop,d},events,count);}return;
            case 0x20:audioCommand(p,{CommandKind::Stop,d},events,count);return;
            case 0x1d:audioCommand(p,{CommandKind::FollowSource,d},events,count);return;
            case 0x5d:audioCommand(p,{p.shift[d]?CommandKind::FollowSource:CommandKind::Sync,d},events,count);return;
            case 0x5e:audioCommand(p,{CommandKind::SetCue,d},events,count);return;
            case 0x5f:audioCommand(p,{CommandKind::Cue,d},events,count);return;
            case 0x61:setValue(p,HeadphoneA+d,1-value(p,HeadphoneA+d));return;
            case 0x1e:setValue(p,param(d,Slip),1-control(p,d,Slip));return;
            case 0x62:case 0x22:setValue(p,FocusDeck,d);return;
            default:break;}}
        if(kind==0xb0){switch(key){
            case 0x4f:setValue(p,param(d,Level),v/127.);
                if(!v&&p.beatpad.faderStart[d]){p.beatpad.faderStart[d]=false;audioCommand(p,{CommandKind::CueStop,d},events,count);}return;
            case 0x4b:case 0x4c:case 0x4d:case 0x4e:case 0x49:
            case 0x0b:case 0x0c:case 0x0d:case 0x0e:case 0x09:beatpadKnob(p,d,key,v);return;
            case 0x43:case 0x44:case 0x63:case 0x03:case 0x04:case 0x23:{
                const bool stack=key<0x40||p.shift[d]||p.stackTouch[d];
                audioCommand(p,{stack?CommandKind::StackJog:CommandKind::Jog,d,0,double(int(v)-64)},events,count);return;}
            default:return;}}
    }
    if(kind==0xe0&&ch<4){const unsigned d=ch&1u;const unsigned raw=key|(v<<7u);setValue(p,param(d,Rate),std::pow(2.,(double(raw)-8192)/8192));return;}
    if(ch==4){
        if(kind==0xb0&&(key==0x47||key==0x48))p.beatpad.eqLayout(key==0x48);
        else if(kind==0xb0&&key==0x45)setValue(p,Crossfade,v/127.);
        else if(kind==0xb0&&key==0x43)setValue(p,HeadphoneMix,v/127.);
        else if(on&&key==0x01)audioCommand(p,{CommandKind::Record,static_cast<unsigned>(value(p,FocusDeck))},events,count);
        else if(kind==0xb0&&(key==0x42||key==0x02)){const unsigned d=static_cast<unsigned>(value(p,FocusDeck));const int delta=int(v)-64;
            const unsigned target=key==0x02?PadBank:Layer;setValue(p,param(d,target),control(p,d,target)+delta);}
        else if(on&&key==0x46)setValue(p,FocusDeck,1-value(p,FocusDeck));
    }
}
void beatpadFeedback(Plugin& p,const clap_output_events_t* output,unsigned frames) noexcept {
    if(!output||!output->try_push||!value(p,Controller)){p.ledInit=false;return;}
    p.ledTick+=frames;
    // Prime once, then send only changed LEDs. The Refresh LEDs button recovers
    // a newly connected hardware return without periodic idle MIDI traffic.
    const bool refresh=p.refreshLeds.exchange(false)||!p.ledInit;
    if(refresh){for(auto& cache:p.padLeds)cache.fill(-1);for(auto& cache:p.jogLeds)cache.fill(-1);p.headphoneLeds.fill(-1);}
    if(!refresh&&p.ledTick<p.sampleRate.load()/20)return;p.ledTick=0;
    const auto* doc=p.hazard.load();if(!doc)return;
    // The firmware stores all eight pad pages independently, including their
    // Shift pages. Prime them all; changing a physical mode then lights pads
    // immediately, before the next audio callback. 0=off, 127=white/held.
    constexpr uint8_t colors[]{1,22,48,11,42,30,53,15};
    for(unsigned d=0;d<2;++d){const unsigned bank=static_cast<unsigned>(control(p,d,PadBank));
        for(unsigned key=0;key<128;++key){const unsigned mode=(key&63)/8,n=key%8;
            const bool keyboard=mode==4&&control(p,d,SamplerPads)!=0;
            const int pitch=keyboardNote(p,d,n);
            const bool store=mode==0&&(key&64u);
            const bool loaded=store?bool(doc->decks[d].layers[static_cast<unsigned>(control(p,d,Layer))].asset):padAvailable(p,*doc,d,mode,bank,n);
            const auto state=PadFeedback::unpack(p.padFeedback[d][n].load(std::memory_order_relaxed));
            const bool held=(state.held||(store&&state.pulse))&&state.matches(mode,bank,keyboard);
            const unsigned lights=p.performanceLights[d].load();
            const bool latched=(mode==1&&(lights&(1u<<n)))||(mode==2&&(lights&(1u<<(8+n))))||(mode==5&&(lights&(1u<<(16+n))))
                ||(mode==7&&n==4&&(p.capture.state.load()==1||p.capture.state.load()==2)&&p.capture.from==d);
            uint8_t color=held||latched?127:!loaded?0:colors[mode];
            if(loaded&&!held&&mode==4){if(keyboard)color=(pitch-int(control(p,d,KeyboardRoot)))%12==0?35:11;
                else if(bank*8+n==unsigned(control(p,d,Layer)))color=35;}
            if(p.padLeds[d][key]==color)continue;
            clap_event_midi_t e{};e.header.size=sizeof(e);e.header.space_id=CLAP_CORE_EVENT_SPACE_ID;e.header.type=CLAP_EVENT_MIDI;e.port_index=0;
            e.data[0]=static_cast<uint8_t>(0x92+d);e.data[1]=static_cast<uint8_t>(key);e.data[2]=color;
            // Failed pushes stay dirty and retry; never cache unsent LEDs.
            if(output->try_push(output,&e.header))p.padLeds[d][key]=color;
        }}
    // Beatpad-family Vinyl RIM protocol: 67 = 24-position red needle,
    // 68 = blue behavior, 69..6C = four RGB sections (8-color palette).
    // Reloop Beatpad MidiMap v1.2, p4; Beatpad 2's map supplements pad/EQ changes.
    // https://www.reloop.com/media/custom/upload/Reloop-Reloop_Beatpad_MidiMap_v1.2.pdf
    for(unsigned d=0;d<2;++d){
        const bool recording=(p.capture.state.load()==1||p.capture.state.load()==2)&&p.capture.to==d;
        const bool loaded=doc->decks[d].base()!=nullptr;
        const bool stack=control(p,d,StackNavigation)!=0||p.performance[d].riding||p.shift[d];
        double position=control(p,d,Position);
        // Multi-layer Follow Source shows the actual layer blend as well. No
        // free-running animation or source-time cycle can obscure Stack Cycle.
        if(stack||doc->decks[d].stack.count>1)position=p.playing[d].load()?p.stackPosition[d].load():control(p,d,StackNavigation)!=0
            ?control(p,d,StackPosition):doc->decks[d].stack.count>1?control(p,d,Layer)/(doc->decks[d].stack.count-1):0;
        else if(p.playing[d].load()){
            const unsigned method=static_cast<unsigned>(control(p,d,Playback));
            position=method==1||method==2||method==4||method==7?p.scanPosition[d].load():currentPosition(p,d);
        }
        if(!std::isfinite(position))position=0;
        const int needle=loaded?1+std::min(23,int(std::clamp(position,0.,1.)*24)):0;
        const int color=recording?1:!loaded?0:stack?4:6; // record red / manual yellow / follow cyan
        const std::array<int,6> desired{{needle,0,color,color,color,color}};
        for(unsigned n=0;n<desired.size();++n){if(p.jogLeds[d][n]==desired[n])continue;
            clap_event_midi_t e{};e.header={sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};e.port_index=0;
            e.data[0]=static_cast<uint8_t>(0x90+d);e.data[1]=static_cast<uint8_t>(0x67+n);e.data[2]=static_cast<uint8_t>(desired[n]);
            if(output->try_push(output,&e.header))p.jogLeds[d][n]=static_cast<int16_t>(desired[n]);
        }
    }
    const unsigned width=sampleNeonBusWidth(decksLayout(static_cast<unsigned>(value(p,Format))));
    const bool routed=deckCueOutput(static_cast<unsigned>(value(p,HeadphoneOutput)),width,value(p,Stems)!=0)>=0;
    for(unsigned d=0;d<2;++d){const int light=routed&&value(p,HeadphoneA+d)!=0?127:0;if(p.headphoneLeds[d]==light)continue;
        clap_event_midi_t e{};e.header={sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};e.port_index=0;
        e.data[0]=static_cast<uint8_t>(0x90+d);e.data[1]=0x61;e.data[2]=static_cast<uint8_t>(light);
        if(output->try_push(output,&e.header))p.headphoneLeds[d]=static_cast<int16_t>(light);}
    p.ledInit=true;
}
