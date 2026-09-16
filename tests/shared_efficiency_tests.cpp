#include "shared_efficiency_api.h"
#include "s3g_ambi_encoder_basis.h"
#include <algorithm>
#include <array>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

int main(int argc, char** argv) {
    const bool benchmark = argc == 2 && std::string(argv[1]) == "--benchmark";
    // Prove the consumed coefficients match, including non-square channel
    // counts, poles and zero direction; the unused low-order suffix is zero.
    const int savedRounding = std::fegetround();
    for (const int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        if (std::fesetround(mode) != 0) return 2;
        unsigned random=1743;
        for (unsigned n=0; n<1030; ++n) {
            s3g::Vec3 direction {};
            if (n<6) {
                const std::array<s3g::Vec3,6> special {{{0,0,0},{0,0,1},{0,0,-1},{1,0,0},{0,1,0},{-1,0,0}}};
                direction=special[n];
            } else {
                float c[3];
                for (auto& x:c) { random=random*1664525u+1013904223u; x=float(int(random>>8)-0x7fffff)/8388608.f; }
                direction={c[0],c[1],c[2]};
            }
            const auto reference=s3g::acnSn3dBasis7(direction);
            for (unsigned channels=0; channels<=64; ++channels) {
                const auto candidate=s3g::ambiEncoderBasisForChannels(direction, channels);
                if (std::memcmp(reference.data(), candidate.data(), channels*sizeof(float))) {
                    std::cerr << "basis mismatch: " << n << ',' << channels << ',' << mode << '\n'; return 1;
                }
                if (channels<=16)
                    for(unsigned c=16;c<64;++c) if(candidate[c]!=0.f) return 1;
            }
        }
    }
    std::fesetround(savedRounding);
    using Clock=std::chrono::steady_clock;
    constexpr unsigned maxFrames=512;
    std::array<std::vector<float>,64> input, output[2];
    std::array<const float*,64> in {};
    std::array<float*,64> out[2] {};
    for(unsigned ch=0;ch<64;++ch) {
        input[ch].resize(maxFrames); in[ch]=input[ch].data();
        for(unsigned i=0;i<maxFrames;++i)
            input[ch][i]=.1f*std::sin(float(i+ch*23)*.073f)+.05f*std::cos(float(i*17+ch)*.313f);
        for(unsigned side=0;side<2;++side) { output[side][ch].resize(maxFrames); out[side][ch]=output[side][ch].data(); }
    }
    const char* names[] {"Water","Wind","Insect","Pyrosphere","Formant","Spectral8","Spectral24"};
    uint64_t comparisons=0;
    for(unsigned kind=0;kind<7;++kind) {
        const auto type=static_cast<EfficiencyEngine>(kind);
        double audibleEnergy=0;
        for(const double rate : {44100.,48000.,96000.}) {
            if(benchmark && rate!=48000.) continue;
            std::unique_ptr<EfficiencyProbe> probes[2] {efficiencyReference(type), efficiencyCandidate(type)};
            for(auto& p:probes) p->prepare(rate, benchmark ? 2048 : 512);
            const unsigned presets=benchmark ? 1 : probes[0]->presets();
            std::vector<double> times[2];
            for(unsigned preset=0;preset<presets;++preset) {
                for(auto& p:probes) { p->reset(); p->configure(preset,0,22,3); }
                const unsigned blocks=benchmark ? 500 : 48;
                for(unsigned b=0;b<blocks;++b) {
                    constexpr unsigned sizes[] {1,15,16,17,64,128,257,512};
                    const unsigned frames=benchmark ? 512 : sizes[b%8];
                    if(!benchmark && b%6==0) {
                        constexpr unsigned orders[] {1,3,7,2,4,3,7,1};
                        constexpr unsigned voices[] {8,22,64};
                        for(auto& p:probes) p->configure(preset,b+1,voices[(b/6)%3],orders[b/6]);
                    }
                    if(!benchmark && b==29) for(auto& p:probes) p->reset();
                    for(unsigned execution=0;execution<2;++execution) {
                        const unsigned side=(b+execution)&1;
                        for(unsigned ch=0;ch<probes[side]->channels();++ch)
                            std::fill(output[side][ch].begin(),output[side][ch].end(),std::numeric_limits<float>::quiet_NaN());
                        const auto start=Clock::now();
                        probes[side]->process(in.data(),out[side].data(),frames);
                        const auto end=Clock::now();
                        if(b>=10) times[side].push_back(std::chrono::duration<double,std::micro>(end-start).count());
                    }
                    for(unsigned ch=0;ch<probes[0]->channels();++ch) for(unsigned f=0;f<frames;++f) {
                        const float a=output[0][ch][f], z=output[1][ch][f];
                        ++comparisons; audibleEnergy+=double(z)*z;
                        if(!std::isfinite(a)||!std::isfinite(z)||std::memcmp(&a,&z,sizeof(float))) {
                            std::cerr << names[kind] << " mismatch rate/preset/block/channel/frame "
                                << rate << '/' << preset << '/' << b << '/' << ch << '/' << f
                                << " values " << a << ',' << z << '\n'; return 1;
                        }
                    }
                    const auto a=probes[0]->inspect(), z=probes[1]->inspect();
                    if(a!=z) { std::cerr<<names[kind]<<" telemetry mismatch\n"; return 1; }
                }
                // Reprepare the same objects: invalidate caches for new rate,
                // FFT size and band layout, not just a fresh constructor.
                if(!benchmark) for(auto& p:probes) p->prepare(preset%2 ? rate : rate*.75, preset%2 ? 512 : 1024);
            }
            for(auto& t:times) std::sort(t.begin(),t.end());
            std::cout << names[kind] << " rate=" << rate << " reference_us=" << times[0][times[0].size()/2]
                << " optimized_us=" << times[1][times[1].size()/2]
                << " ratio=" << times[1][times[1].size()/2]/times[0][times[0].size()/2] << '\n';
        }
        if(!(audibleEnergy>0)) { std::cerr<<names[kind]<<" comparison was silent\n"; return 1; }
    }
    std::cout << "PASS: " << comparisons << " bit-identical finite audio samples, basis, telemetry, order/voice/parameter changes, resets and reprepare\n";
}
