#pragma once
#include "../plugins/common/s3g_generated_sample_media.h"
#include "sample_snapshot_checks.h"
#include <thread>
#include <stdexcept>

namespace generated_test {
namespace media=s3g::sample_storage;
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project;
inline Project* active=nullptr;
struct Project {
    std::filesystem::path root;
    std::string file,directory;
    unsigned additions=0,removals=0;
    std::atomic<unsigned> callbacks{0};
    media::ReaperHostBridge bridge{1,nullptr,registerObject,function};
    clap_host_t host{CLAP_VERSION_INIT,this,"Generated Audio Test","s3g","","1",extension,
        [](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t* h){static_cast<Project*>(h->host_data)->callbacks.fetch_add(1);}};
    Project(){char path[]="/private/tmp/s3g-generated-media.XXXXXX";require(mkdtemp(path),"create owned fixture directory");root=path;
        file=(root/"Session.rpp").string();directory=(root/"Media").string();active=this;}
    ~Project(){active=nullptr;std::error_code error;std::filesystem::remove_all(root,error);}
    static const void* extension(const clap_host_t* h,const char* id){return !strcmp(id,"cockos.reaper_extension")?&static_cast<Project*>(h->host_data)->bridge:nullptr;}
    static void* context(const clap_host_t* h,int selector){return selector==3?h->host_data:selector==4?reinterpret_cast<void*>(uintptr_t(1)):nullptr;}
    static void* enumerate(int index,char* path,int size){if(index||!active)return nullptr;snprintf(path,size,"%s",active->file.c_str());return active;}
    static void mediaPath(void*,char* path,int size){snprintf(path,size,"%s",active->directory.c_str());}
    static int registerObject(const char* name,void*){if(!strcmp(name,"file_in_project_ex2"))++active->additions;
        if(!strcmp(name,"-file_in_project_ex2"))++active->removals;return 1;}
    static void* function(const char* name){
        if(!strcmp(name,"clap_get_reaper_context"))return reinterpret_cast<void*>(&context);
        if(!strcmp(name,"EnumProjects"))return reinterpret_cast<void*>(&enumerate);
        if(!strcmp(name,"GetProjectPathEx"))return reinterpret_cast<void*>(&mediaPath);return nullptr;}
};
inline std::shared_ptr<const s3g::sample::SampleAsset> audio(unsigned channels=2,float gain=.3f){
    auto asset=std::make_shared<s3g::sample::SampleAsset>();asset->sampleRate=48000;asset->channelCount=channels;
    for(unsigned ch=0;ch<channels;++ch){asset->channels[ch].resize(48000);
        for(unsigned i=0;i<48000;++i)asset->channels[ch][i]=gain*std::sin(i*.13f)*(ch%2?-1:1);}
    return asset;
}
template<class Service,class Ready> void collect(Service service,Ready ready){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do{service();if(ready())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
    require(false,"background collection did not finish");
}
inline void save(const clap_plugin_t* plugin,sample_snapshot_test::Buffer& b){
    const auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    require(state&&state->save(plugin,&b.output),"save generated-audio state");}
}
