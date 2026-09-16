#include "windows_pyro_coefficients_api.h"
#include <xmmintrin.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

int main(int argc,char**) {
    const bool benchmark=argc>1; const unsigned saved=_mm_getcsr();
    struct Restore {unsigned mode;~Restore(){_mm_setcsr(mode);}} restore{saved};
    using Clock=std::chrono::steady_clock;
    unsigned long long checked=0; double maxDifference=0;
    std::puts("rate,preset,blocks,reference_median_ms,candidate_median_ms,max_abs_difference");
    for(double rate: {44100.0,48000.0,96000.0}) for(unsigned preset=0;preset<18;++preset) {
        if(benchmark&&(rate!=48000.0||preset!=0))continue;
        // Exercise original and cached calculations under matching host modes.
        const unsigned mode=(saved|0x1f80u)&~0x607fu;
        _mm_setcsr(mode|((preset&1)?0x8040u:0));
        void* instances[2]{pyro_reference_create(rate),pyro_candidate_create(rate)};
        struct Owners{void** p;~Owners(){pyro_reference_destroy(p[0]);pyro_candidate_destroy(p[1]);}} owners{instances};
        std::array<std::array<std::vector<float>,64>,2> data;
        std::array<std::array<float*,64>,2> pointers{};
        for(unsigned m=0;m<2;++m)for(unsigned c=0;c<64;++c){data[m][c].resize(512);pointers[m][c]=data[m][c].data();}
        std::vector<double> times[2]; const unsigned blocks=benchmark?1200:24;
        const unsigned frameChoices[]{1,15,16,17,64,128,257,512};
        for(unsigned b=0;b<blocks;++b){
            const unsigned frames=benchmark?512:frameChoices[b%8];
            if(b==0||(!benchmark&&b%3==0)){
                const unsigned voiceChoices[]{8,22,64},orderChoices[]{1,3,7};
                const unsigned voices=benchmark?22:voiceChoices[(b/3)%3];
                const unsigned order=benchmark?3:orderChoices[(b/6)%3];
                pyro_reference_configure(instances[0],preset,voices,order,benchmark?0:b);
                pyro_candidate_configure(instances[1],preset,voices,order,benchmark?0:b);
            }
            if(!benchmark&&b==17){pyro_reference_reset(instances[0]);pyro_candidate_reset(instances[1]);}
            for(unsigned execution=0;execution<2;++execution){
                const unsigned m=(b+execution)&1;
                for(unsigned c=0;c<64;++c)std::fill(data[m][c].begin(),data[m][c].end(),std::numeric_limits<float>::quiet_NaN());
                const auto start=Clock::now();
                if(m)pyro_candidate_process(instances[m],pointers[m].data(),64,frames);
                else pyro_reference_process(instances[m],pointers[m].data(),64,frames);
                const auto end=Clock::now();
                if(b>=3)times[m].push_back(std::chrono::duration<double,std::milli>(end-start).count());
            }
            for(unsigned c=0;c<64;++c)for(unsigned f=0;f<frames;++f){
                const float a=data[0][c][f],z=data[1][c][f];++checked;
                maxDifference=std::max(maxDifference,std::abs(double(a)-z));
                if(!std::isfinite(a)||!std::isfinite(z)||std::memcmp(&a,&z,sizeof(float))){
                    std::fprintf(stderr,"FAIL rate=%.0f preset=%u block=%u channel=%u frame=%u values=%.9g,%.9g\n",rate,preset,b,c,f,a,z);return 1;
                }
            }
            double snapshots[2][160]{};
            const unsigned count=pyro_reference_inspect(instances[0],snapshots[0]);
            if(count!=pyro_candidate_inspect(instances[1],snapshots[1])||std::memcmp(snapshots[0],snapshots[1],count*sizeof(double))){std::fprintf(stderr,"FAIL: evolving voice/score state differs\n");return 1;}
            checked+=count;
        }
        for(auto& t:times)std::sort(t.begin(),t.end());
        std::printf("%.0f,%u,%u,%.9f,%.9f,%.12g\n",rate,preset,blocks,times[0][times[0].size()/2],times[1][times[1].size()/2],maxDifference);
    }
    std::fprintf(stderr,"PASS: %llu bit-identical finite audio/state comparisons; max_abs_difference=%.12g; alternating call order. Native engine test, not REAPER acceptance.\n",checked,maxDifference);
    return 0;
}
