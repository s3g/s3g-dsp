// White-box state regression only. This test never creates or installs an editor.
#include "../plugins/clap_sample_kit/s3g_sample_kit_clap.cpp"
#include "sample_snapshot_checks.h"
#include "generated_sample_media_checks.h"

void generatedKitChecks(){
    using namespace generated_test;Project project;
    const auto* plugin=createPlugin(&factory,&project.host,descriptor.id);require(plugin&&plugin->init(plugin),"Kit fixture init");auto& p=*self(plugin);
    p.storageMode=StorageMode::Project;auto original=audio(2);p.chopSourceAsset=original;
    p.controlAssets[0][0]=original;p.controlAssets[1][0]=original;
    const auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    sample_snapshot_test::Buffer pending;save(plugin,pending);require(pending.bytes.size()>48000*2*4,"Kit pending slices embed");
    require(!std::filesystem::exists(project.directory),"Kit state save never collects files");
    collect([&]{serviceLoads(p);},[&]{return !p.generatedMedia.reference(original,p.host).empty();});
    sample_snapshot_test::Buffer ready;save(plugin,ready);
    require(ready.bytes.size()==sizeof(StateHeader)+sizeof(SavedStateV3Body),"Kit generated slices are reference-only");
    require(project.additions==1,"Kit shared slices/source export once");
    require(sample_snapshot_test::repeated(plugin,state,ready.bytes.size()),"Kit repeated generated snapshots");
    auto edited=audio(2,.7f);p.controlAssets[0][0]=edited;
    sample_snapshot_test::Buffer changed;save(plugin,changed);require(changed.bytes.size()>ready.bytes.size(),"Kit new version embeds until ready");
    collect([&]{serviceLoads(p);},[&]{return !p.generatedMedia.reference(edited,p.host).empty();});
    p.controlAssets[0][0]=original;sample_snapshot_test::Buffer undo;save(plugin,undo);require(undo.bytes==ready.bytes,"Kit undo reference stays reusable");
    require(state->load(plugin,&ready.input),"Kit reference recall");
    collect([&]{serviceLoads(p);},[&]{return p.controlAssets[0][0]&&p.controlAssets[0][0]->channels==original->channels;});
    plugin->destroy(plugin);require(project.additions==project.removals,"Kit registrations balanced");
}

int main(){@autoreleasepool{
    try{generatedKitChecks();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    clap_host_t host{CLAP_VERSION_INIT,nullptr,"Snapshot Test","s3g","","1",
        [](const clap_host_t*,const char*)->const void*{return nullptr;},
        [](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
    const auto* plugin=createPlugin(&factory,&host,descriptor.id);
    if(!plugin||!plugin->init(plugin))return 1;
    auto& p=*self(plugin);auto audio=std::make_shared<SampleAsset>();audio->sampleRate=48000;audio->channelCount=2;
    for(unsigned ch=0;ch<2;++ch)audio->channels[ch].assign(48000,ch?-.37f:.37f);
    p.controlAssets[0][0]=audio;p.samplePaths[0][0]="/missing/snapshot-kit.wav";p.storageMode=StorageMode::Project;
    const auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    const size_t metadata=sizeof(StateHeader)+sizeof(SavedStateV3Body);
    bool ok=sample_snapshot_test::repeated(plugin,state,metadata);
    // Destructive slices without a file retain complete PCM, including shared
    // variations: one payload per unique immutable source, not per pad cell.
    p.samplePaths[0][0].clear();p.controlAssets[1][0]=audio;
    ok=sample_snapshot_test::repeated(plugin,state,metadata+48000*2*sizeof(float))&&ok;
    plugin->destroy(plugin);
    std::cout<<(ok?"Kit reference/pathless/shared-source snapshots passed\n":"Kit snapshot failure\n");
    return ok?0:1;
}}
