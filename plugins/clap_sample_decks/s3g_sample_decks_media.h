#pragma once
#include "../common/s3g_sample_storage.h"
#include "../common/s3g_audio_file_export.h"
#include "s3g_sample_decks_document.h"
#include <chrono>

namespace s3g::decks {
// Main-thread locator cache, separate from the immutable audio document. Media
// collection must not publish a DSP revision, stop a deck, or create an undo.
struct DeckProjectMedia {
    std::weak_ptr<const s3g::sample::SampleAsset> asset;
    std::string source;
    s3g::sample_storage::ProjectLocation project;
    s3g::sample_storage::ProjectCopyResult copy;
    bool attempted=false;
    std::chrono::steady_clock::time_point retryAfter{};
    bool matches(const LayerAudio& layer,const s3g::sample_storage::ProjectLocation& location) const {
        return attempted&&layer.asset&&asset.lock()==layer.asset&&source==layer.path
            &&project.project==location.project&&project.projectFilePath==location.projectFilePath
            &&project.mediaDirectory==location.mediaDirectory;
    }
};
struct DeckMediaJob {
    unsigned deck=0,layer=0;
    DeckProjectMedia media;
    std::shared_ptr<const s3g::sample::SampleAsset> audio;
    std::string name;
};
using DeckMediaResult=std::vector<DeckMediaJob>;

// Worker-only: commit immutable, float WAV media for captured/destructively
// edited PCM. Never overwrite an earlier audio version (host undo may still
// reference it), and never publish a locator until collection is verified.
inline void collectDeckMedia(DeckMediaJob& job) {
    namespace storage=s3g::sample_storage;
    auto& result=job.media.copy;
    if(!job.media.source.empty()) {
        result=storage::copyFileIntoProject(job.media.project,job.media.source);
        if(result.success)return;
    }
    if(!job.audio||!job.audio->valid()||!job.media.project.available())return;
#if (defined(__APPLE__) && defined(__OBJC__)) || defined(_WIN32)
    struct TemporaryExport {
        std::filesystem::path directory,file;
        ~TemporaryExport(){std::error_code error;
            if(!file.empty())std::filesystem::remove(file,error);
            if(!directory.empty())std::filesystem::remove(directory,error);}
    } temporary;
    std::error_code error;
    const auto directory=storage::detail::uniqueTemporaryPath(
        std::filesystem::temp_directory_path()/"s3g-decks-export");
    if(!std::filesystem::create_directory(directory,error)){
        result.error="COULD NOT PREPARE GENERATED AUDIO EXPORT";return;}
    temporary.directory=directory;
    temporary.file=directory/("decks-"+storage::detail::recognizableStem(
        std::filesystem::u8path(job.name))+".wav");
    std::array<const float*,16> channels{};
    for(unsigned ch=0;ch<job.audio->channelCount;++ch)channels[ch]=job.audio->channels[ch].data();
    if(!s3g::audio_file::writePlanarFloatWaveAtomically(temporary.file.u8string(),
        job.audio->sampleRate,job.audio->channelCount,job.audio->frameCount(),channels.data(),result.error))return;
    result=storage::copyFileIntoProject(job.media.project,temporary.file.u8string());
#else
    result.error="GENERATED AUDIO EXPORT IS NOT AVAILABLE ON THIS PLATFORM";
#endif
}
}
