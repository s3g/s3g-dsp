#pragma once
#include "s3g_sample_decks_parameters.h"
#include "s3g_sample_cutups_analysis.h"
#include "s3g_sample_decks_peaks.h"
#include <memory>

namespace s3g::decks {
struct LayerAudio {
    std::shared_ptr<const SampleAsset> asset;
    std::shared_ptr<const WavesetMap> wavesets;
    std::shared_ptr<const DeckWavePeaks> waveform;
    CutupsLaneMetadata analysis;
    std::string path, name;
    double start=0,end=1;
    SampleNeonSliceLayout slices=equalSampleNeonSliceLayout(8);
    bool relative=false;
};
struct DeckBookmark { bool set=false; uint8_t layer=0; double position=0; };
struct DeckDocument {
    std::array<LayerAudio,32> layers;
    std::array<DeckBookmark,8> bookmarks {};
    NeonStack stack;
    std::array<std::shared_ptr<const NeonStack>,6> formatStacks;
    uint64_t revision=0;
    static unsigned formatIndex(unsigned channels) {return channels==1?0:channels==2?1:channels==4?2:channels==8?3:channels==9?4:5;}
    const LayerAudio* playbackBase(unsigned selected) const {
        const auto* chosen=selected<32?layers[selected].asset.get():nullptr;
        if(!chosen)chosen=base();if(!chosen)return nullptr;
        for(const auto& layer:layers)if(layer.asset&&layer.asset->channelCount==chosen->channelCount)return &layer;
        return nullptr;
    }
    const NeonStack& playbackStack(unsigned selected) const {
        const auto* layer=playbackBase(selected);
        if(layer){const auto& group=formatStacks[formatIndex(layer->asset->channelCount)];if(group)return *group;}
        return stack;
    }
    unsigned recordTarget(unsigned selected) const {return selected<32&&!layers[selected].asset?selected:firstEmpty();}
    void rebuild() {
        for(auto& cue:bookmarks)if(cue.set&&!layers[cue.layer].asset)cue={};
        stack={};
        for (unsigned n=0;n<32;++n) {
            auto& l=layers[n];
            if (!l.asset) continue;
            stack.count=static_cast<uint8_t>(n+1);
            stack.layers[n]={l.asset.get(),l.start,l.end,l.wavesets.get()};
            stack.cutupsMetadata[n]=l.analysis;
            stack.sourceBpms[n]=l.analysis.tempoValid?static_cast<float>(l.analysis.analyzedBpm):120.f;
            for (unsigned s=0;s<l.slices.sliceCount;++s)
                stack.fragments[stack.fragmentCount++]=neonDescribeFragment(stack.layers[n],n,
                    l.start+(l.end-l.start)*l.slices.boundaries[s],l.start+(l.end-l.start)*l.slices.boundaries[s+1]);
        }
        stack.primarySliceCount=layers[0].slices.sliceCount;
        stack.primarySlices=layers[0].slices.boundaries;
        formatStacks={};std::array<bool,6> formats{};unsigned total=0;
        for(const auto& layer:layers)if(layer.asset){auto& present=formats[formatIndex(layer.asset->channelCount)];if(!present){present=true;++total;}}
        if(total>1)for(unsigned format=0;format<6;++format)if(formats[format]){
            auto group=std::make_shared<NeonStack>(stack);group->skipEmptyLayers=true;group->fragmentCount=0;
            unsigned first=32;
            for(unsigned n=0;n<32;++n)if(layers[n].asset){if(formatIndex(layers[n].asset->channelCount)!=format)group->layers[n]={};else if(first==32)first=n;}
            for(unsigned n=0;n<stack.fragmentCount;++n)if(group->layers[stack.fragments[n].layer].asset)group->fragments[group->fragmentCount++]=stack.fragments[n];
            if(first<32){group->primarySliceCount=layers[first].slices.sliceCount;group->primarySlices=layers[first].slices.boundaries;}
            formatStacks[format]=std::move(group);
        }
    }
    unsigned firstEmpty() const { for(unsigned i=0;i<32;++i) if(!layers[i].asset) return i; return 32; }
    const SampleAsset* base() const { for(const auto& l:layers) if(l.asset)return l.asset.get(); return nullptr; }
};
struct Document {
    std::array<DeckDocument,2> decks;
    std::string action="INITIAL";
    std::array<float,kParamCount> savedControls {};
};
inline void analyzeLayer(LayerAudio& l) {
    if(!l.asset) return;
    l.waveform=buildDeckWavePeaks(*l.asset);
    l.wavesets=analyzeNeonWavesets(l.asset);
    l.analysis=analyzeCutupsAsset(*l.asset);
}
inline std::shared_ptr<const SampleAsset> cropLayer(const LayerAudio& layer) {
    if(!layer.asset || layer.end<=layer.start) return {};
    const auto& source=*layer.asset;
    const unsigned first=std::min(source.frameCount()-1,static_cast<unsigned>(layer.start*source.frameCount()));
    const unsigned end=std::min(source.frameCount(),static_cast<unsigned>(std::ceil(layer.end*source.frameCount())));
    if(end<=first) return {};
    auto copy=std::make_shared<SampleAsset>(); copy->sampleRate=source.sampleRate; copy->channelCount=source.channelCount;
    for(unsigned ch=0;ch<source.channelCount;++ch) copy->channels[ch].assign(source.channels[ch].begin()+first,source.channels[ch].begin()+end);
    return copy;
}
// Slice coordinates are relative to the trimmed source, not the whole file.
// Snap all channels together using Neon's shared minimum-discontinuity search.
inline void snapDeckSliceMarker(const LayerAudio& layer,SampleNeonSliceLayout& slices,unsigned marker) {
    if(!layer.asset||marker==0||marker>=slices.sliceCount||layer.end<=layer.start)return;
    const auto& a=*layer.asset;const double first=layer.start*a.frameCount(),span=(layer.end-layer.start)*a.frameCount();
    const auto target=static_cast<uint32_t>(std::clamp(first+span*slices.boundaries[marker],0.,double(a.frameCount()-1)));
    const auto radius=static_cast<uint32_t>(a.sampleRate*.005);
    const auto lo=std::max(static_cast<uint32_t>(std::floor(first+span*slices.boundaries[marker-1]))+1,target>radius?target-radius:1);
    const auto hi=std::min(static_cast<uint32_t>(std::ceil(first+span*slices.boundaries[marker+1]))-1,std::min(a.frameCount()-1,target+radius));
    if(lo<=hi){const double at=(nearestSafeSampleBoundary(a,target,lo,hi)-first)/span;
        if(at>slices.boundaries[marker-1]+1.e-6&&at<slices.boundaries[marker+1]-1.e-6)slices.boundaries[marker]=at;}
}
inline bool removeDeckSliceMarker(SampleNeonSliceLayout& slices,unsigned marker) {
    if(marker==0||marker>=slices.sliceCount)return false;
    for(unsigned i=marker;i<slices.sliceCount;++i)slices.boundaries[i]=slices.boundaries[i+1];
    slices.boundaries[slices.sliceCount--]=0;return true;
}
} // namespace s3g::decks
