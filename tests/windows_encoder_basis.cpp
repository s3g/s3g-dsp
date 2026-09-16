#include "s3g_windows_encoder_basis.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <xmmintrin.h>

int main(){
    const unsigned saved=_mm_getcsr();
    struct Restore{unsigned mode;~Restore(){_mm_setcsr(mode);}} restore{saved};
    unsigned long long checked=0;
    std::vector<s3g::Vec3> directions{{0,0,0},{0,0,1},{0,0,-1},{1,0,0},{0,1,0}};
    unsigned random=1743;
    for(unsigned i=0;i<1024;++i){float c[3];for(auto& v:c){random=random*1664525u+1013904223u;v=float(int(random>>8)-0x7fffff)/8388608.0f;}directions.push_back({c[0],c[1],c[2]});}
    for(unsigned rounding: {0u,0x2000u,0x4000u,0x6000u}){
        _mm_setcsr(((saved|0x1f80u)&~0x607fu)|rounding);
        for(auto direction:directions){
            const auto reference=s3g::acnSn3dBasis7(direction);
            for(unsigned channels=0;channels<=64;++channels){
                const auto candidate=s3g::windowsEncoderBasis(direction,channels);
                for(unsigned c=0;c<channels;++c){++checked;if(std::memcmp(&reference[c],&candidate[c],sizeof(float))){std::fprintf(stderr,"FAIL channels=%u coefficient=%u rounding=%u\n",channels,c,rounding);return 1;}}
            }
        }
    }
    _mm_setcsr(saved);
    using Clock=std::chrono::steady_clock;volatile float sink=0;
    for(unsigned channels: {4u,16u,64u}){
        std::vector<double> elapsed[2];
        for(unsigned batch=0;batch<24;++batch)for(unsigned execution=0;execution<2;++execution){
            const unsigned m=(batch+execution)&1;const auto start=Clock::now();
            for(unsigned n=0;n<4096;++n){const auto p=directions[(n+batch)%directions.size()];const auto b=m?s3g::windowsEncoderBasis(p,channels):s3g::acnSn3dBasis7(p);sink=sink+b[n%channels];}
            elapsed[m].push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
        }
        for(auto& e:elapsed)std::sort(e.begin(),e.end());
        std::printf("channels=%u reference_batch_median_ms=%.6f candidate_batch_median_ms=%.6f\n",channels,elapsed[0][12],elapsed[1][12]);
    }
    std::printf("PASS: %llu bit-identical consumed coefficients; all 0-64 channel counts, poles/zero/random directions, four rounding modes. sink=%g\n",checked,double(sink));
    return 0;
}
