// White-box mutable-loop snapshot/storage test; never an installable editor.
#include "../plugins/clap_crcltr/s3g_crcltr_clap.cpp"
#include "generated_sample_media_checks.h"
int main(){@autoreleasepool{try{
    using namespace generated_test;Project project;
    const auto* plugin=createPlugin(&factory,&project.host,descriptor.id);require(plugin&&plugin->init(plugin),"Circulator fixture init");auto& p=*self(plugin);
    require(plugin->activate(plugin,48000,1,256),"Circulator activate");auto original=audio();
    for(unsigned loop=0;loop<2;++loop)require(p.dsp.restoreLoop(loop,original->channels[0].data(),original->channels[1].data(),48000,48000),"restore loop fixture");
    sample_snapshot_test::Buffer pending;save(plugin,pending);require(pending.bytes.size()==sizeof(SavedStateHeader)+48000*4*4,"Circulator pending embeds exact stereo loops");
    require(!std::filesystem::exists(project.directory),"Circulator snapshot does no disk work");
    collect([&]{serviceGeneratedLoops(p);},[&]{return !p.generatedMedia.reference(p.savedLoopAssets[1],p.host).empty();});
    sample_snapshot_test::Buffer ready;save(plugin,ready);require(ready.bytes.size()<1024,"Circulator loops become references");
    const auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    const auto cached=p.savedLoopAssets;require(sample_snapshot_test::repeated(plugin,state,ready.bytes.size()),"Circulator repeated compact snapshots");
    require(p.savedLoopAssets==cached,"Circulator unchanged snapshots never copy PCM");
    sample_snapshot_test::Buffer preset;require(savePresetState(plugin,&preset.output)&&preset.bytes.size()==pending.bytes.size(),"Circulator explicit presets remain self-contained");
    require(state->load(plugin,&ready.input),"Circulator v4 project recall");
    require(p.savedLoopAssets[0]->channels==original->channels,"Circulator exact stereo recall");
    sample_snapshot_test::Buffer immediate;save(plugin,immediate);require(immediate.bytes.size()==ready.bytes.size(),"Circulator immediate resave reference-only");
    // An overdub updates PCM in place but must invalidate the exported version.
    auto params=p.params;params.recordMode=s3g::CrcltrRecordMode::Overdub;params.record=true;p.dsp.setParams(params);
    std::array<float,256> in{},outL{},outR{};in.fill(.15f);p.dsp.process(in.data(),in.data(),outL.data(),outR.data(),256);
    params.record=false;p.dsp.setParams(params);p.dsp.process(in.data(),in.data(),outL.data(),outR.data(),256);
    sample_snapshot_test::Buffer overdub;save(plugin,overdub);require(overdub.bytes.size()>1024,"Circulator overdub cannot reuse old files");
    require(p.savedLoopAssets[0]->channels!=original->channels,"Circulator overdub snapshot has changed PCM");
    const auto overdubAssets=p.savedLoopAssets;
    collect([&]{serviceGeneratedLoops(p);},[&]{return !p.generatedMedia.reference(p.savedLoopAssets[1],p.host).empty();});
    sample_snapshot_test::Buffer latest;save(plugin,latest);require(latest.bytes.size()<1024,"Circulator overdub reference-only after export");
    ready.cursor=0;require(state->load(plugin,&ready.input),"Circulator host undo restores first version");
    require(p.savedLoopAssets[0]->channels==original->channels,"Circulator undo does not recall overdub");
    require(state->load(plugin,&latest.input),"Circulator redo restores overdub");
    require(p.savedLoopAssets[0]->channels==overdubAssets[0]->channels,"Circulator exact overdub redo");
    p.dsp.clearLoop(0);sample_snapshot_test::Buffer cleared;save(plugin,cleared);
    SavedStateHeader header;memcpy(&header,cleared.bytes.data(),sizeof(header));require(header.loopFrames[0]==0,"Circulator clear invalidates cache");
    // Contention is reported, never a torn snapshot or a realtime lock.
    p.loopSnapshots[1].begin();require(!cacheLoopSnapshots(p),"Circulator rejects in-progress writes");p.loopSnapshots[1].end(48000);
    // Stress the actual mirror concurrently, with unmistakable version data.
    s3g::CrcltrSnapshot mirror;mirror.prepare(4096);std::atomic<bool> finished{false};
    std::thread writer([&]{for(unsigned n=1;n<=2000;++n){mirror.begin();for(unsigned f=0;f<4096;++f)mirror.write(f,float(n),-float(n));mirror.end(4096);}finished=true;});
    std::array<float,4096> left{},right{};unsigned coherent=0;bool torn=false;
    do{const auto version=mirror.version();if(mirror.frames()==4096&&mirror.copy(version,left.data(),right.data(),4096)){
        ++coherent;for(unsigned f=0;f<4096;++f)torn|=left[f]!=left[0]||right[f]!=-left[0];}
    }while(!finished.load());writer.join();
    require(mirror.copy(mirror.version(),left.data(),right.data(),4096),"Circulator final concurrent mirror copy");
    require(!torn&&left[0]==2000&&right[0]==-2000,"Circulator concurrent snapshots never mix versions");
    std::cout<<"Coherent concurrent loop snapshots: "<<coherent<<'\n';
    project.file.clear();sample_snapshot_test::Buffer unsaved;save(plugin,unsaved);require(unsaved.bytes.size()>1024,"Circulator unsaved host embeds");
    plugin->deactivate(plugin);plugin->destroy(plugin);require(project.additions==project.removals,"Circulator registrations balanced");
    std::cout<<"Circulator generated loop snapshots passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}}
