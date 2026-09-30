#pragma once
#include "s3g_sample_asset.h"
#include <memory>

namespace s3g::decks {
// Immutable display-only data. Build with the source analysis, never in paint
// or process(). The extra lane merges extrema, not samples: opposite-polarity
// spatial/stereo channels must not cancel in the stack overview.
struct DeckPeak {
    float low=0,high=0;
    void add(float v) noexcept {low=std::min(low,v);high=std::max(high,v);}
    void add(DeckPeak v) noexcept {low=std::min(low,v.low);high=std::max(high,v.high);}
};
struct DeckWavePeaks {
    static constexpr unsigned baseStride=64;
    struct Level {unsigned stride=0;std::vector<std::vector<DeckPeak>> lanes;};
    std::vector<Level> levels;
    uint64_t bytes() const noexcept {
        uint64_t result=0;for(const auto& level:levels)for(const auto& lane:level.lanes)result+=lane.size()*sizeof(DeckPeak);return result;
    }
    // Conservative extrema at overview resolution (never miss a transient).
    // At sample resolution read at most 63 frames. Otherwise at most 17 small
    // peak bins, independent of file length and of the number of source lanes.
    DeckPeak query(const s3g::sample::SampleAsset& a,unsigned begin,unsigned end,int channel,unsigned* reads=nullptr) const noexcept {
        DeckPeak result;begin=std::min(begin,a.frameCount());end=std::min(end,a.frameCount());
        if(begin>=end)return result;
        const unsigned ch=channel<0?a.channelCount:std::min(unsigned(channel),unsigned(a.channelCount-1));
        if(end-begin<baseStride||levels.empty()){
            // Missing analysis is not an excuse to scan a large source in UI.
            if(end-begin>=baseStride)return result;
            for(unsigned c=channel<0?0:ch;c<(channel<0?a.channelCount:ch+1);++c)
                for(unsigned n=begin;n<end;++n){result.add(a.channels[c][n]);if(reads)++*reads;}
            return result;
        }
        unsigned level=0;
        while(level+1<levels.size()&&levels[level+1].stride<=(end-begin)/4)++level;
        const auto& data=levels[level];const auto& lane=data.lanes[ch];
        const unsigned first=begin/data.stride,last=(end-1)/data.stride;
        for(unsigned n=first;n<=last;++n){result.add(lane[n]);if(reads)++*reads;}
        return result;
    }
};
inline std::shared_ptr<const DeckWavePeaks> buildDeckWavePeaks(const s3g::sample::SampleAsset& a) {
    auto result=std::make_shared<DeckWavePeaks>();if(!a.frameCount()||!a.channelCount)return result;
    DeckWavePeaks::Level base;base.stride=DeckWavePeaks::baseStride;
    const unsigned bins=(a.frameCount()-1)/base.stride+1;
    base.lanes.resize(a.channelCount+1);for(auto& lane:base.lanes)lane.resize(bins);
    for(unsigned ch=0;ch<a.channelCount;++ch)for(unsigned bin=0;bin<bins;++bin){
        auto& peak=base.lanes[ch][bin];const unsigned end=std::min(a.frameCount(),(bin+1)*base.stride);
        for(unsigned n=bin*base.stride;n<end;++n)peak.add(a.channels[ch][n]);
        base.lanes[a.channelCount][bin].add(peak);
    }
    result->levels.push_back(std::move(base));
    while(result->levels.back().lanes[0].size()>1){
        const auto& previous=result->levels.back();DeckWavePeaks::Level next;next.stride=previous.stride*4;
        next.lanes.resize(a.channelCount+1);
        for(unsigned ch=0;ch<next.lanes.size();++ch){auto& lane=next.lanes[ch];lane.resize((previous.lanes[ch].size()+3)/4);
            for(unsigned n=0;n<previous.lanes[ch].size();++n)lane[n/4].add(previous.lanes[ch][n]);}
        result->levels.push_back(std::move(next));
    }
    return result;
}
} // namespace s3g::decks
