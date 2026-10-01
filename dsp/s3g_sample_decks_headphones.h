#pragma once
#include "s3g_ambisonic_stereo_decoder.h"
#include <array>

namespace s3g::sample {
// These are plug-in output channels; the host maps them to any audio interface.
// 0 = off, 1 = first free pair, 2..17 = explicit pairs 1/2..31/32.
inline int deckCueOutput(unsigned selection,unsigned width,bool stems) noexcept {
    const unsigned occupied=width*((stems&&width*3<=32)?3:1);
    if(!selection)return -1;
    const unsigned first=selection==1?(occupied+1)&~1u:(selection-2)*2;
    return first>=occupied&&first+2<=32?static_cast<int>(first):-1;
}
struct DeckHeadphoneSettings {
    unsigned output=1,format=0,fold=0;
    bool stems=false;
    std::array<bool,2> selected{};
    float mix=0,gainDb=-6; // 0 = selected decks (pre-fader), 1 = main mix
};
class DeckHeadphones {
public:
    using Matrix=std::array<std::array<float,16>,2>;
    void prepare(double rate) {
        alpha_=static_cast<float>(1-std::exp(-1/(.005*rate)));
        // Reuse the suite's ACN/SN3D decoder, preparing fixed XY matrices once.
        // A stereo monitor projection, not a binaural/HRTF renderer.
        s3g::AmbiStereoDecoder decoder;decoder.prepare(rate);
        for(unsigned order=1;order<=3;++order){s3g::AmbiStereoParams p;p.order=order;
            p.stereoWidthPercent=100;p.autogain=s3g::AmbiStereoAutogain::Off;
            decoder.setParams(p);auto& m=ambi_[order-1];
            for(unsigned ch=0;ch<(order+1)*(order+1);++ch){m[0][ch]=decoder.leftCoeffs()[ch];m[1][ch]=decoder.rightCoeffs()[ch];}
            normalize(m,(order+1)*(order+1));}
        for(unsigned mode=0;mode<3;++mode)for(unsigned layout=0;layout<2;++layout){const unsigned width=layout?8:4;auto& m=mc_[mode][layout];
            for(unsigned ch=0;ch<width;++ch){if(mode==0)m[ch%2][ch]=1;
                else if(mode==1){const double pan=std::sin((-180./width+ch*360./width)*3.141592653589793/180.);
                    m[0][ch]=std::sqrt((1-pan)*.5);m[1][ch]=std::sqrt((1+pan)*.5);}
                else m[0][ch]=m[1][ch]=1;}
            normalize(m,width);}
        reset();
    }
    void reset() noexcept {weights_={};lastOutput_=-1;lastFormat_=99;lastFold_=99;}
    Matrix matrix(unsigned format,unsigned fold) const noexcept {
        if(format>=3)return ambi_[std::min(5u,format)-3];
        if(format)return mc_[std::min(2u,fold)][format==2?1:0];
        Matrix m{};m[0][0]=m[1][1]=1;return m;
    }
    void render(const DeckHeadphoneSettings& s,unsigned width,const float* const* a,const float* const* b,
        float* const* output,unsigned frames) noexcept {
        const int first=deckCueOutput(s.output,width,s.stems);
        if(first<0){reset();return;} // Never replace or sum into main/stem outputs.
        if(first!=lastOutput_||s.format!=lastFormat_||s.fold!=lastFold_)weights_={};
        lastOutput_=first;lastFormat_=s.format;lastFold_=s.fold;
        const auto m=matrix(s.format,s.fold);
        const float gain=std::pow(10.f,std::clamp(s.gainDb,-60.f,0.f)/20.f);
        const float mix=std::clamp(s.mix,0.f,1.f);
        // Average both selected decks for headroom; one deck retains its level.
        const float divisor=std::max(1,int(s.selected[0])+int(s.selected[1]));
        const std::array<float,3> target{{s.selected[0]?gain*(1-mix)/divisor:0,s.selected[1]?gain*(1-mix)/divisor:0,gain*mix}};
        for(unsigned n=0;n<frames;++n){for(unsigned i=0;i<3;++i)weights_[i]+=alpha_*(target[i]-weights_[i]);
            std::array<float,2> stereo{};
            for(unsigned ch=0;ch<width;++ch){const float v=a[ch][n]*weights_[0]+b[ch][n]*weights_[1]+output[ch][n]*weights_[2];
                for(unsigned ear=0;ear<2;++ear)stereo[ear]+=v*m[ear][ch];}
            // Safety ceiling on the monitor only; main mix/captures stay intact.
            for(unsigned ear=0;ear<2;++ear)output[first+ear][n]=std::isfinite(stereo[ear])?std::clamp(stereo[ear],-1.f,1.f):0;
        }
    }
private:
    static void normalize(Matrix& m,unsigned width) noexcept {
        float peak=1;for(const auto& row:m){float sum=0;for(unsigned ch=0;ch<width;++ch)sum+=std::abs(row[ch]);peak=std::max(peak,sum);}
        for(auto& row:m)for(auto& v:row)v/=peak;
    }
    std::array<Matrix,3> ambi_{};
    std::array<std::array<Matrix,2>,3> mc_{};
    std::array<float,3> weights_{};
    float alpha_=1;int lastOutput_=-1;unsigned lastFormat_=99,lastFold_=99;
};
} // namespace s3g::sample
