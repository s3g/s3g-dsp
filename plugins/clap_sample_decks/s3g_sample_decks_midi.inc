// Device-independent dispatch. New controllers supply one decode/feedback pair;
// they do not fork the engine or editor, and do not change existing profile IDs.
void commitPad(Plugin& p,unsigned d,unsigned pad,std::array<SampleNeonEvent,64>& events,unsigned& count) noexcept {
    auto pending=p.pendingPads[d][pad];p.pendingPads[d][pad].active=false;
    setValue(p,param(d,PadMode),pending.mode);
    audioCommand(p,{CommandKind::Pad,d,pad,pending.velocity},events,count);
}
void notesMidi(Plugin& p,const uint8_t* bytes,std::array<SampleNeonEvent,64>& events,unsigned& count) noexcept {
    const unsigned kind=bytes[0]&0xf0u,ch=bytes[0]&15u,key=bytes[1]&127u,v=bytes[2]&127u;
    const bool on=kind==0x90&&v!=0,off=kind==0x80||(kind==0x90&&v==0);
    if(value(p,MidiChannel)!=0&&ch!=unsigned(value(p,MidiChannel))-1)return;
    if((on||off)&&key>=36&&key<=37)audioCommand(p,{on?CommandKind::Cue:CommandKind::Stop,key-36},events,count);
    if(kind==0xb0&&key==16)setValue(p,Crossfade,v/127.);
}
#include "s3g_sample_decks_beatpad.inc"
struct ControllerAdapter {
    ControllerProfile profile;
    void (*decode)(Plugin&,const uint8_t*,std::array<SampleNeonEvent,64>&,unsigned&) noexcept;
    void (*feedback)(Plugin&,const clap_output_events_t*,unsigned) noexcept;
};
constexpr ControllerAdapter controllerAdapters[]{
    {ControllerProfile::Notes,notesMidi,nullptr},
    {ControllerProfile::Beatpad2,beatpadMidi,beatpadFeedback}
};
const ControllerAdapter& controllerAdapter(const Plugin& p) noexcept {
    return controllerAdapters[static_cast<unsigned>(value(p,Controller))];
}
void midi(Plugin& p,const uint8_t* bytes,std::array<SampleNeonEvent,64>& events,unsigned& count) noexcept {
    if((bytes[0]&0xf0)==0xb0&&(bytes[1]==120||bytes[1]==123)){
        p.stackTouch={};p.shift={};
        for(unsigned d=0;d<2;++d)audioCommand(p,{CommandKind::Stop,d},events,count);return;
    }
    controllerAdapter(p).decode(p,bytes,events,count);
}
void feedback(Plugin& p,const clap_output_events_t* output,unsigned frames) noexcept {
    if(const auto send=controllerAdapter(p).feedback)send(p,output,frames);
    else p.ledInit=false;
}
