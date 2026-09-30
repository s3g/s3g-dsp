// Beatpad 2 adapter. Protocol addresses and LED color numbers stay here;
// DSP/editor actions and semantic pad feedback are device-independent.
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
        if(on){auto& pending=p.pendingPads[d][pad];pending={true,mode,static_cast<unsigned>(p.sampleRate.load()*.002),v/127.f};}
        else {if(p.pendingPads[d][pad].active)commitPad(p,d,pad,events,count);audioCommand(p,{CommandKind::Pad,d,pad,0},events,count);}return;
    }
    if(ch<2){const unsigned d=ch;
        if((on||off)&&key==0x1c){p.shift[d]=on;
            // Changing gesture mid-touch must not leave the time platter held.
            if(on&&p.touching[d].load()){audioCommand(p,{CommandKind::Touch,d,0,0},events,count);p.stackTouch[d]=true;}return;}
        if(on&&key>=0x48&&key<=0x4f){setValue(p,param(d,PadMode),key-0x48);p.refreshLeds.store(true);return;}
        if(on&&key>=8&&key<=15)return; // shifted mode keys include the firmware velocity toggle
        if((on||off)&&(key==0x63||key==0x23||key==0x64||key==0x24)){
            if(on&&(p.shift[d]||key<0x40)){p.stackTouch[d]=true;return;}
            if(off&&p.stackTouch[d]){p.stackTouch[d]=false;return;}
            audioCommand(p,{CommandKind::Touch,d,0,on?1.:0.},events,count);return;}
        if(on){switch(key){
            case 0x60:audioCommand(p,{CommandKind::Play,d},events,count);return;
            case 0x20:audioCommand(p,{CommandKind::Stop,d},events,count);return;
            case 0x5d:audioCommand(p,{CommandKind::Sync,d},events,count);return;
            case 0x5e:audioCommand(p,{CommandKind::SetCue,d},events,count);return;
            case 0x5f:audioCommand(p,{CommandKind::Cue,d},events,count);return;
            case 0x1e:setValue(p,param(d,Slip),1-control(p,d,Slip));return;
            case 0x62:case 0x22:setValue(p,FocusDeck,d);return;
            default:break;}}
        if(kind==0xb0){switch(key){
            case 0x4f:setValue(p,param(d,Level),v/127.);return;
            case 0x4b:setValue(p,param(d,Gain),v<=64?(double(v)-64)*24/64:(double(v)-64)*12/63);return;
            case 0x4c:case 0x4d:case 0x4e:setValue(p,param(d,High+key-0x4c),v<=64?(double(v)-64)*24/64:(double(v)-64)*12/63);return;
            case 0x49:setValue(p,param(d,Filter),(double(v)-64)/64);return;
            case 0x43:case 0x44:case 0x63:case 0x03:case 0x04:case 0x23:{
                const bool stack=key<0x40||p.shift[d]||p.stackTouch[d];
                audioCommand(p,{stack?CommandKind::StackJog:CommandKind::Jog,d,0,double(int(v)-64)},events,count);return;}
            default:return;}}
    }
    if(kind==0xe0&&ch<4){const unsigned d=ch&1u;const unsigned raw=key|(v<<7u);setValue(p,param(d,Rate),std::pow(2.,(double(raw)-8192)/8192));return;}
    if(ch==4){
        if(kind==0xb0&&key==0x45)setValue(p,Crossfade,v/127.);
        else if(on&&key==0x01)audioCommand(p,{CommandKind::Record,static_cast<unsigned>(value(p,FocusDeck))},events,count);
        else if(kind==0xb0&&(key==0x42||key==0x02)){const unsigned d=static_cast<unsigned>(value(p,FocusDeck));const int delta=int(v)-64;
            const unsigned target=key==0x02?PadBank:Layer;setValue(p,param(d,target),control(p,d,target)+delta);}
        else if(on&&key==0x46)setValue(p,FocusDeck,1-value(p,FocusDeck));
    }
}
void beatpadFeedback(Plugin& p,const clap_output_events_t* output,unsigned frames) noexcept {
    if(!output||!output->try_push||!value(p,Controller)){p.ledInit=false;return;}
    p.ledTick+=frames;p.ledRefreshTick+=frames;
    const bool refresh=p.refreshLeds.exchange(false)||!p.ledInit||p.ledRefreshTick>=p.sampleRate.load()*2;
    if(refresh){for(auto& cache:p.padLeds)cache.fill(-1);p.ledRefreshTick=0;}
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
            const bool loaded=padAvailable(p,*doc,d,mode,bank,n);
            const auto state=PadFeedback::unpack(p.padFeedback[d][n].load(std::memory_order_relaxed));
            const bool held=state.held&&state.matches(mode,bank,keyboard);
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
        }}p.ledInit=true;
}
