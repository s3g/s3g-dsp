// Compare independently built Cocoa/VSTGUI MIDI plugins: metadata, state and exact MIDI output.
#include <algorithm>
#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool ok = true;
void check(bool result, const char *label) {
  if (!result) {
    std::cerr << label << '\n';
    ok = false;
  }
}
struct Stream {
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  clap_ostream_t out{
      this,
      [](const clap_ostream_t *s, const void *data, uint64_t size) -> int64_t {
        auto &v = static_cast<Stream *>(s->ctx)->bytes;
        size = std::min<uint64_t>(size, 7);
        const auto *b = static_cast<const uint8_t *>(data);
        v.insert(v.end(), b, b + size);
        return int64_t(size);
      }};
  clap_istream_t in{
      this, [](const clap_istream_t *s, void *data, uint64_t size) -> int64_t {
        auto &v = *static_cast<Stream *>(s->ctx);
        size = std::min<uint64_t>({size, 11, v.bytes.size() - v.pos});
        std::memcpy(data, v.bytes.data() + v.pos, size);
        v.pos += size;
        return int64_t(size);
      }};
};
struct Events {
  std::vector<clap_event_param_value_t> values;
  clap_input_events_t input{
      this,
      [](const clap_input_events_t *e) -> uint32_t {
        return uint32_t(static_cast<const Events *>(e->ctx)->values.size());
      },
      [](const clap_input_events_t *e,
         uint32_t i) -> const clap_event_header_t * {
        return &static_cast<const Events *>(e->ctx)->values[i].header;
      }};
  void add(clap_id id, double value) {
    clap_event_param_value_t e{};
    e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE,
                0};
    e.param_id = id;
    e.value = value;
    e.note_id = -1;
    e.port_index = e.channel = e.key = -1;
    values.push_back(e);
  }
};
struct Module {
  void *handle = nullptr;
  const clap_plugin_entry_t *entry = nullptr;
  const clap_plugin_t *plugin = nullptr;
  const clap_plugin_params_t *params = nullptr;
  const clap_plugin_state_t *state = nullptr;
  const clap_plugin_audio_ports_t *ports = nullptr;
  std::vector<clap_param_info_t> infos;
  clap_host_t host{};
  bool open(const char *path, const char *id) {
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Cocoa/VSTGUI parity";
    host.vendor = "s3g";
    host.version = "1";
    host.url = "";
    host.get_extension = [](const clap_host_t *, const char *) -> const void * {
      return nullptr;
    };
    host.request_process = host.request_callback =
        host.request_restart = [](const clap_host_t *) {};
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      std::cerr << dlerror() << '\n';
      return false;
    }
    entry =
        static_cast<const clap_plugin_entry_t *>(dlsym(handle, "clap_entry"));
    if (!entry || !entry->init(path))
      return false;
    const auto *factory = static_cast<const clap_plugin_factory_t *>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory)
      return false;
    plugin = factory->create_plugin(factory, &host, id);
    if (!plugin || !plugin->init(plugin))
      return false;
    params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    ports = static_cast<const clap_plugin_audio_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (!params || !state)
      return false;
    for (unsigned i = 0; i < params->count(plugin); ++i) {
      clap_param_info_t info{};
      if (!params->get_info(plugin, i, &info))
        return false;
      infos.push_back(info);
    }
    return true;
  }
  ~Module() {
    if (plugin)
      plugin->destroy(plugin);
    if (entry)
      entry->deinit();
    if (handle)
      dlclose(handle);
  }
};
void compareParams(Module &a, Module &b) {
  for (const auto &info : a.infos) {
    double x = 0., y = 0.;
    check(a.params->get_value(a.plugin, info.id, &x) &&
              b.params->get_value(b.plugin, info.id, &y) && std::isfinite(x) &&
              std::isfinite(y) && std::abs(x - y) < 1.e-5,
          info.name);
  }
}
void exchangeState(Module &a, Module &b) {
  Stream stream;
  check(a.state->save(a.plugin, &stream.out), "chunked state save");
  check(b.state->load(b.plugin, &stream.in), "cross-build chunked state load");
  Stream roundtrip;
  check(b.state->save(b.plugin,&roundtrip.out) && roundtrip.bytes==stream.bytes,
      "cross-build state bytes round-trip unchanged");
  // NIM's Last Loop Length is a transient read-only display: the original
  // loader selects the last stored loop, not the previously cleared loop.
  // Load the same state into both modules before comparing those displays.
  stream.pos=0;
  check(a.state->load(a.plugin,&stream.in),"reference state reload");
  compareParams(a, b);
  std::vector<double> before;
  for (const auto &i : b.infos) {
    double value = 0.;
    b.params->get_value(b.plugin, i.id, &value);
    before.push_back(value);
  }
  // Every truncation must fail transactionally, including the final byte.
  const auto bytes = stream.bytes;
  for (size_t length :
       {size_t(0), size_t(3), bytes.size() / 2, bytes.size() - 1}) {
    stream.bytes.assign(bytes.begin(), bytes.begin() + length);
    stream.pos = 0;
    check(!b.state->load(b.plugin, &stream.in), "truncated state rejected");
    for (size_t i = 0; i < b.infos.size(); ++i) {
      double value = 0.;
      b.params->get_value(b.plugin, b.infos[i].id, &value);
      check(value == before[i],
            "failed state load leaves parameters unchanged");
    }
  }
}

struct MidiEvents {
  std::vector<clap_event_midi_t> messages;
  clap_input_events_t input{this,
    [](const clap_input_events_t* i)->uint32_t{return static_cast<MidiEvents*>(i->ctx)->messages.size();},
    [](const clap_input_events_t* i,uint32_t n)->const clap_event_header_t*{return &static_cast<MidiEvents*>(i->ctx)->messages.at(n).header;}};
  void add(uint8_t a,uint8_t b,uint8_t c,uint32_t time) {
    clap_event_midi_t m{};m.header={sizeof(m),time,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};
    m.data[0]=a;m.data[1]=b;m.data[2]=c;messages.push_back(m);
  }
};
struct Output {
  std::vector<uint64_t> midi;
  clap_output_events_t output{this,[](const clap_output_events_t* o,const clap_event_header_t* h)->bool {
    auto& self=*static_cast<Output*>(o->ctx);
    if(h->type==CLAP_EVENT_MIDI) {
      auto* m=reinterpret_cast<const clap_event_midi_t*>(h);
      self.midi.push_back((uint64_t(h->time)<<32)|(uint64_t(m->port_index)<<24)|(uint64_t(m->data[0])<<16)|(uint64_t(m->data[1])<<8)|m->data[2]);
    }
    return true;
  }};
};
void compareMidi(Module& a,Module& b,unsigned scene) {
  const bool nim=std::strstr(a.plugin->desc->id,"nim-gesture")!=nullptr;
  check(a.plugin->activate(a.plugin,48000.,1,128)&&b.plugin->activate(b.plugin,48000.,1,128),"activate both MIDI builds");
  check(a.plugin->start_processing(a.plugin)&&b.plugin->start_processing(b.plugin),"start both MIDI builds");
  uint64_t count=0;
  for(unsigned block=0;block<500;++block) {
    Events changes;
    if(nim) {
      if(block==0){changes.add(1,1);changes.add(2,1);}
      if(block==32)changes.add(1,0);
      if(block==150)changes.add(5,1300);
      if(block==420)changes.add(3,1);
    } else if(block%50==0) {
      // Automate a bounded subset, keeping Enabled and the MIDI routing usable.
      for(auto index:{size_t(1),size_t(2),size_t(3),size_t(4)}) {
        const auto& i=a.infos[index];changes.add(i.id,.1+double((block+scene*7+index*13)%80)/100.);
      }
      changes.add(1,1);
    }
    a.params->flush(a.plugin,&changes.input,nullptr);b.params->flush(b.plugin,&changes.input,nullptr);
    MidiEvents input;
    if(nim && (block<32 || block==100 || block==200)) {
      for(unsigned n=0;n<3;++n) {
        const uint16_t id=5+n,v=(block*397+n*1100+scene*911)%16384;
        input.add(0xbf,99,id>>7,n*17);input.add(0xbf,98,id&127,n*17);
        input.add(0xbf,6,v>>7,n*17);input.add(0xbf,38,v&127,n*17);
      }
    }
    if(block%32==0)input.add(0x90,60+scene,100,70);
    if(block%32==16)input.add(0x80,60+scene,0,70);
    if(!nim && block%24==0)input.add(0xb0,1,block%128,90);
    clap_event_transport_t tr{};
    tr.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE|CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo=112+scene*13;tr.song_pos_beats=clap_beattime(double(block)*128./48000.*tr.tempo/60.*CLAP_BEATTIME_FACTOR);
    clap_process_t proc{};proc.frames_count=128;proc.steady_time=block*128;proc.transport=&tr;proc.in_events=&input.input;
    Output oa,ob;proc.out_events=&oa.output;check(a.plugin->process(a.plugin,&proc)!=CLAP_PROCESS_ERROR,"Cocoa MIDI process");
    proc.out_events=&ob.output;check(b.plugin->process(b.plugin,&proc)!=CLAP_PROCESS_ERROR,"VSTGUI MIDI process");
    check(oa.midi==ob.midi,"exact MIDI bytes, ordering and frame offsets");count+=oa.midi.size();compareParams(a,b);
  }
  check(count>0,"nonempty MIDI parity scene");
  std::cout<<"scene "<<scene<<": "<<count<<" identical MIDI events\n";
  a.plugin->stop_processing(a.plugin);b.plugin->stop_processing(b.plugin);
  a.plugin->deactivate(a.plugin);b.plugin->deactivate(b.plugin);
}
} // namespace
int main(int argc,char** argv) {
  if(argc!=4){std::cerr<<"usage: midi-parity cocoa-binary vstgui-binary plugin-id\n";return 2;}
  for(unsigned scene=0;scene<3;++scene) {
    Module a,b;if(!a.open(argv[1],argv[3])||!b.open(argv[2],argv[3]))return 2;
    check(a.infos.size()==b.infos.size(),"parameter count");if(a.infos.size()!=b.infos.size())return 1;
    for(size_t n=0;n<a.infos.size();++n) {
      const auto& x=a.infos[n];const auto& y=b.infos[n];
      check(x.id==y.id&&x.flags==y.flags&&x.min_value==y.min_value&&x.max_value==y.max_value&&x.default_value==y.default_value
        &&!std::strcmp(x.name,y.name)&&!std::strcmp(x.module,y.module),"unchanged parameter metadata");
    }
    auto* na=static_cast<const clap_plugin_note_ports_t*>(a.plugin->get_extension(a.plugin,CLAP_EXT_NOTE_PORTS));
    auto* nb=static_cast<const clap_plugin_note_ports_t*>(b.plugin->get_extension(b.plugin,CLAP_EXT_NOTE_PORTS));
    check(na&&nb,"MIDI ports");
    if(na&&nb)for(bool input:{true,false}) {
      check(na->count(a.plugin,input)==nb->count(b.plugin,input),"note port count");
      for(uint32_t n=0;n<na->count(a.plugin,input);++n) {
        clap_note_port_info_t x{},y{};
        check(na->get(a.plugin,n,input,&x)&&nb->get(b.plugin,n,input,&y)&&x.id==y.id
          &&x.supported_dialects==y.supported_dialects&&x.preferred_dialect==y.preferred_dialect&&!std::strcmp(x.name,y.name),"note port metadata");
      }
    }
    for(bool input:{true,false})check((!a.ports||a.ports->count(a.plugin,input)==0)&&(!b.ports||b.ports->count(b.plugin,input)==0),"MIDI-only has no audio ports");
    const bool nim=std::strstr(argv[3],"nim-gesture")!=nullptr;
    Events setup;
    for(size_t n=0;n<a.infos.size();++n) {
      const auto& i=a.infos[n];if(i.flags&CLAP_PARAM_IS_READONLY)continue;
      double x=i.default_value;
      if(scene>0 && (!nim||i.id==5))x=i.min_value+double((n*13+scene*17)%97)/97.*(i.max_value-i.min_value);
      if(i.flags&CLAP_PARAM_IS_STEPPED)x=std::round(x);setup.add(i.id,x);
    }
    a.params->flush(a.plugin,&setup.input,nullptr);b.params->flush(b.plugin,&setup.input,nullptr);
    compareParams(a,b);exchangeState(a,b);exchangeState(b,a);compareMidi(a,b,scene);
    exchangeState(a,b);exchangeState(b,a);
  }
  if(ok)std::cout<<argv[3]<<" MIDI metadata/state/output parity passed\n";
  return ok?0:1;
}
