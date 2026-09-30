// White-box capture/storage test only; never installed as a plug-in/editor.
#include "../plugins/clap_sample_rings/s3g_sample_rings_clap.cpp"
#include "generated_sample_media_checks.h"
int main(){@autoreleasepool{try{
    using namespace generated_test;Project project;
    const auto* plugin=createPlugin(&factory,&project.host,descriptor.id);require(plugin&&plugin->init(plugin),"Rings fixture init");auto& p=*self(plugin);
    p.storageMode=StorageMode::Project;p.sampleRate=48000;
    auto original=audio(8);auto& capture=p.captures[0];capture.samples.resize(48000*8);
    for(unsigned f=0;f<48000;++f)for(unsigned ch=0;ch<8;++ch)capture.samples[f*8+ch]=original->channels[ch][f];
    capture.readyFrames=48000;capture.readyChannels=8;capture.ready=true;
    finalizeCaptures(p);require(p.sources[0]&&p.sources[0]->channels==original->channels,"Rings finalizes exact capture");original=p.sources[0];
    sample_snapshot_test::Buffer pending;save(plugin,pending);require(pending.bytes.size()>48000*8*4,"Rings pending capture embeds");
    require(!std::filesystem::exists(project.directory),"Rings save does no file collection");
    collect([&]{serviceLoads(p);},[&]{return !p.generatedMedia.reference(original,p.host).empty();});
    sample_snapshot_test::Buffer ready;save(plugin,ready);require(ready.bytes.size()==sizeof(SavedState),"Rings capture reference-only");
    const auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    require(sample_snapshot_test::repeated(plugin,state,ready.bytes.size()),"Rings repeated generated snapshots");
    require(state->load(plugin,&ready.input),"Rings project capture recall");
    collect([&]{serviceLoads(p);},[&]{return p.sources[0]&&p.sources[0]->channels==original->channels;});
    p.storageMode=StorageMode::Embed;sample_snapshot_test::Buffer embedded;save(plugin,embedded);
    require(embedded.bytes.size()>ready.bytes.size(),"Rings explicit Embed remains portable");
    p.storageMode=StorageMode::Project;
    // A fresh take clears an old registered locator before exporting its PCM.
    for(auto& x:capture.samples)x*=.5f;capture.readyFrames=48000;capture.readyChannels=8;capture.ready=true;finalizeCaptures(p);
    const auto newer=p.sources[0];require(newer!=original,"Rings new capture immutable version");
    sample_snapshot_test::Buffer next;save(plugin,next);require(next.bytes.size()>ready.bytes.size(),"Rings new capture never reuses old locator");
    collect([&]{serviceLoads(p);},[&]{return !p.generatedMedia.reference(newer,p.host).empty();});
    sample_snapshot_test::Buffer current;save(plugin,current);require(current.bytes.size()==ready.bytes.size(),"Rings second capture compact");
    plugin->destroy(plugin);require(project.additions==project.removals,"Rings registrations balanced");
    std::cout<<"Rings generated capture snapshots passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}}
