#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace s3g::sample {
// Double-buffered post-FX history. Freezing a take swaps buffers in O(1), never
// copies seconds of audio in process(). The hidden live deck keeps running.
class DeckRoll {
public:
    void prepare(double rate) {
        rate_=rate;capacity_=static_cast<unsigned>(std::ceil(rate*4));
        for(auto& bank:audio_)for(auto& ch:bank)ch.assign(capacity_,0);
        reset();
    }
    void reset() noexcept {head_={};valid_={};writing_=0;frozen_=1;width_=0;active_=false;wet_=0;length_=phase_=0;last_={};join_={};joinLeft_=0;}
    bool ready(double seconds) const noexcept {return seconds>0&&seconds*rate_<=valid_[writing_];}
    bool start(double seconds) noexcept {
        if(!ready(seconds))return false;
        frozen_=writing_;writing_^=1;head_[writing_]=valid_[writing_]=0;
        length_=std::max(1u,static_cast<unsigned>(std::round(seconds*rate_)));
        first_=(head_[frozen_]+capacity_-length_)%capacity_;phase_=0;
        active_=true;join_=last_;joinLeft_=fadeFrames();return true;
    }
    void release() noexcept {active_=false;}
    bool active() const noexcept {return active_;}
    unsigned historyFrames() const noexcept {return valid_[writing_];}
    void process(float* const* channels,unsigned width,unsigned frames) noexcept {
        if(width_!=width){reset();width_=width;}
        const float step=1.f/fadeFrames();
        for(unsigned n=0;n<frames;++n){
            wet_=std::clamp(wet_+(active_?step:-step),0.f,1.f);
            const unsigned read=(first_+phase_)%capacity_;
            // A short, shared-channel join avoids discontinuities at wrap and
            // entry without changing the musical repeat period.
            const float blend=joinLeft_?1.f-float(joinLeft_)/fadeFrames():1.f;
            for(unsigned ch=0;ch<width;++ch){
                const float live=channels[ch][n];audio_[writing_][ch][head_[writing_]]=live;
                const float loop=length_?join_[ch]+blend*(audio_[frozen_][ch][read]-join_[ch]):live;
                channels[ch][n]=live+wet_*(loop-live);last_[ch]=loop;
            }
            head_[writing_]=(head_[writing_]+1)%capacity_;valid_[writing_]=std::min(capacity_,valid_[writing_]+1);
            if(joinLeft_)--joinLeft_;
            if(length_&&++phase_>=length_){phase_=0;join_=last_;joinLeft_=fadeFrames();}
        }
    }
private:
    unsigned fadeFrames() const noexcept {return std::max(1u,std::min(static_cast<unsigned>(rate_*.003),length_?std::max(1u,length_/8):128u));}
    std::array<std::array<std::vector<float>,16>,2> audio_;
    std::array<unsigned,2> head_{},valid_{};
    std::array<float,16> last_{},join_{};
    double rate_=48000;unsigned capacity_=0,writing_=0,frozen_=1,width_=0,length_=0,first_=0,phase_=0,joinLeft_=0;
    float wet_=0;bool active_=false;
};
}
