#pragma once
#include "s3g_sample_storage.h"
#include "s3g_audio_file_export.h"
#include "s3g_sample_asset.h"
#include <clap/ext/state.h>
#include <chrono>
#include <future>
#include <memory>
#include <vector>

namespace s3g::sample_storage {
// Immutable PCM only. Call service/remember on the main thread, never from
// state.save or process. A reference lookup does no IO and changes no state.
// Weak cache keys retain undo locators, not additional copies of the samples.
class GeneratedSampleMedia {
public:
    using Asset=std::shared_ptr<const s3g::sample::SampleAsset>;
    struct Source {Asset asset;std::string name;};
    ~GeneratedSampleMedia(){if(worker_.valid())worker_.wait();}
    static bool sameProject(const ProjectLocation& a,const ProjectLocation& b){
        return a.project==b.project&&a.projectFilePath==b.projectFilePath&&a.mediaDirectory==b.mediaDirectory;
    }
    std::string reference(const Asset& asset,const ProjectLocation& project) const {
        if(!asset||!project.available())return {};
        for(const auto& entry:entries_)if(entry->asset.lock()==asset&&sameProject(entry->project,project)&&entry->copy.success){
            const auto& path=entry->registration.registered()?entry->registration.absolutePath():entry->copy.absolutePath;
            std::string relative;if(makeProjectRelativePath(project,path,relative))return relative;
        }
        return {};
    }
    std::string reference(const Asset& asset,const clap_host_t* host) const {
        ProjectLocation project;if(!queryProjectLocation(reaperContext(host),project))return {};
        return reference(asset,project);
    }
    // A successfully decoded project reference is already valid for a save
    // immediately following load (before a timer or worker has run).
    void remember(const Asset& asset,const clap_host_t* host,const std::string& absolute){
        ProjectLocation project;const auto context=reaperContext(host);std::string relative;
        if(!asset||!queryProjectLocation(context,project)||!makeProjectRelativePath(project,absolute,relative))return;
        auto& entry=get(asset,project);entry.copy.success=true;entry.copy.absolutePath=absolute;
        (void)entry.registration.reset(context,absolute);
    }
    void service(const clap_host_t* host,const std::vector<Source>& sources,bool enabled=true){
        const auto context=reaperContext(host);bool changed=false;
        if(result_.valid()&&result_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            std::vector<Job> results;
            try{results=result_.get();}catch(...){result_={};return;}
            for(auto& result:results){auto& entry=get(result.audio,result.project);entry.copy=std::move(result.copy);
                entry.retryAfter=std::chrono::steady_clock::now()+std::chrono::seconds(30);
                if(entry.copy.success){(void)entry.registration.reset(context,entry.copy.absolutePath);changed=true;}}
        }
        if(changed&&host&&host->get_extension){const auto* state=static_cast<const clap_host_state_t*>(host->get_extension(host,CLAP_EXT_STATE));
            if(state&&state->mark_dirty)state->mark_dirty(host);}
        if(!enabled||result_.valid())return;
        ProjectLocation project;if(!queryProjectLocation(context,project))return;
        // Keep file registrations even after a weak PCM key expires: REAPER's
        // serialized undo history can still reference that earlier take.
        const auto now=std::chrono::steady_clock::now();std::vector<Job> jobs;
        for(const auto& source:sources){if(!source.asset)continue;auto& entry=get(source.asset,project);
            if(entry.copy.success||now<entry.retryAfter)continue;
            entry.retryAfter=now+std::chrono::seconds(30); // also deduplicates shared layers
            jobs.push_back({source.asset,source.name,project,{}});}
        if(jobs.empty())return;
        try{
            if(worker_.valid())worker_.get();
            std::promise<std::vector<Job>> ready;result_=ready.get_future();
            worker_=std::async(std::launch::async,[host,jobs=std::move(jobs),ready=std::move(ready)]() mutable {
                for(auto& job:jobs){try{collect(job);}catch(...){job.copy.success=false;job.copy.error="GENERATED AUDIO EXPORT FAILED";}}
                ready.set_value(std::move(jobs)); // ready BEFORE the main-thread callback
                if(host&&host->request_callback)host->request_callback(host);
            });
        }catch(...){result_={};}
    }
private:
    struct Entry {std::weak_ptr<const s3g::sample::SampleAsset> asset;ProjectLocation project;ProjectCopyResult copy;
        ProjectFileRegistration registration;std::chrono::steady_clock::time_point retryAfter{};};
    struct Job {Asset audio;std::string name;ProjectLocation project;ProjectCopyResult copy;};
    Entry& get(const Asset& asset,const ProjectLocation& project){
        for(auto& entry:entries_)if(entry->asset.lock()==asset&&sameProject(entry->project,project))return *entry;
        auto entry=std::make_unique<Entry>();entry->asset=asset;entry->project=project;entries_.push_back(std::move(entry));return *entries_.back();
    }
    static void collect(Job& job){
#if (defined(__APPLE__) && defined(__OBJC__)) || defined(_WIN32)
        if(!job.audio||!job.audio->valid()||!job.project.available())return;
        struct Temporary {std::filesystem::path directory,file;~Temporary(){std::error_code ec;
            if(!file.empty())std::filesystem::remove(file,ec);if(!directory.empty())std::filesystem::remove(directory,ec);}} temporary;
        std::error_code error;const auto directory=detail::uniqueTemporaryPath(std::filesystem::temp_directory_path()/"s3g-generated-export");
        if(!std::filesystem::create_directory(directory,error)){job.copy.error="CANNOT PREPARE AUDIO EXPORT";return;}
        temporary.directory=directory;temporary.file=directory/(detail::recognizableStem(std::filesystem::u8path(job.name))+".wav");
        std::array<const float*,16> channels{};for(unsigned ch=0;ch<job.audio->channelCount;++ch)channels[ch]=job.audio->channels[ch].data();
        if(!s3g::audio_file::writePlanarFloatWaveAtomically(temporary.file.u8string(),job.audio->sampleRate,
            job.audio->channelCount,job.audio->frameCount(),channels.data(),job.copy.error))return;
        job.copy=copyFileIntoProject(job.project,temporary.file.u8string());
#else
        job.copy.error="GENERATED AUDIO EXPORT UNAVAILABLE";
#endif
    }
    std::vector<std::unique_ptr<Entry>> entries_;
    std::future<std::vector<Job>> result_;
    std::future<void> worker_;
};
}
