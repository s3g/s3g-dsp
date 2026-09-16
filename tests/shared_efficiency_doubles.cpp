// Exercise the real publication boundary and cursor publisher. No host GUI is
// opened; the protected PCM page proves cursor work cannot scan the audio file.
#include "../plugins/clap_sample_doubles/s3g_sample_doubles_clap.cpp"
#include <iostream>
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
class GuardedSamplePage {
    void* address = nullptr;
    size_t size = 0;
#if defined(_WIN32)
    DWORD previous = 0;
#endif
public:
    explicit GuardedSamplePage(std::vector<float>& samples) {
#if defined(_WIN32)
        SYSTEM_INFO info {}; GetSystemInfo(&info); size=info.dwPageSize;
#else
        size=static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
        // Entire page is strictly inside this test-owned PCM allocation.
        const auto start=reinterpret_cast<uintptr_t>(samples.data());
        const auto page=((start+size-1)/size+1)*size;
        if(page+size>start+samples.size()*sizeof(float)) return;
        address=reinterpret_cast<void*>(page);
#if defined(_WIN32)
        if(!VirtualProtect(address,size,PAGE_NOACCESS,&previous)) address=nullptr;
#else
        if(mprotect(address,size,PROT_NONE)!=0) address=nullptr;
#endif
    }
    ~GuardedSamplePage() {
        if(!address) return;
#if defined(_WIN32)
        DWORD ignored; VirtualProtect(address,size,previous,&ignored);
#else
        mprotect(address,size,PROT_READ|PROT_WRITE);
#endif
    }
    bool active() const { return address!=nullptr; }
};
}

int main(int argc, char** argv) {
    const bool benchmark=argc==2 && std::string(argv[1])=="--benchmark";
    bool ok=true;
    const auto check=[&](bool value, const char* message) {
        if(!value) { ok=false; std::cerr<<message<<'\n'; }
    };
    auto plugin=std::make_unique<Plugin>();
    initializeParams(*plugin);
    plugin->engine.prepare(48000.);
    auto asset=std::make_shared<SampleAsset>();
    asset->sampleRate=48000.; asset->channelCount=2;
    for(unsigned c=0;c<2;++c) asset->channels[c].assign(2*1024*1024, .1f+float(c)*.1f);
    check(publishAsset(*plugin,asset,"",false),"valid sample rejected");
    check(plugin->publishedAsset.load()==asset.get(),"validated asset not published");
    plugin->engine.setPreparedAsset(asset.get());
    DoublesSettings settings;
    settings.start=.125; settings.end=.875;
    publishCursorState(*plugin,asset.get(),settings,true);
    CursorSnapshot before, after;
    check(readCursorSnapshot(*plugin,before),"cursor snapshot unavailable");
    if(benchmark) {
        using Clock=std::chrono::steady_clock;
        std::vector<double> times[2];
        for(unsigned n=0;n<120;++n) for(unsigned execution=0;execution<2;++execution) {
            const unsigned side=(n+execution)&1;
            const auto start=Clock::now();
            // The former Mac path added this complete validation to each
            // cursor publication; include it without timing asset creation.
            if(side==0 && !asset->valid()) return 1;
            publishCursorState(*plugin,asset.get(),settings,false);
            const auto end=Clock::now();
            if(n>=20) times[side].push_back(std::chrono::duration<double,std::micro>(end-start).count());
        }
        for(auto& t:times) std::sort(t.begin(),t.end());
        std::cout<<"Doubles cursor, 43.69s stereo source: reference_us="<<times[0][50]
            <<" optimized_us="<<times[1][50]<<'\n';
    }
    {
        GuardedSamplePage guard(asset->channels[0]);
        if(!guard.active()) { std::cerr<<"could not protect owned PCM page\n"; return 2; }
        for(unsigned n=0;n<100;++n) publishCursorState(*plugin,asset.get(),settings,false);
        check(readCursorSnapshot(*plugin,after),"guarded cursor snapshot unavailable");
        check(before.start==after.start && before.end==after.end
            && before.rateA==after.rateA && before.rateB==after.rateB
            && before.deckA==after.deckA && before.deckB==after.deckB
            && before.activeMask==after.activeMask && before.loop==after.loop
            && before.discontinuity==after.discontinuity,
            "cursor state changed while reusing validated immutable PCM");
    }
    auto invalid=std::make_shared<SampleAsset>(*asset);
    invalid->channels[1][123456]=std::numeric_limits<float>::quiet_NaN();
    check(!publishAsset(*plugin,invalid,"",false),"nonfinite sample reached publication");
    check(plugin->publishedAsset.load()==asset.get(),"invalid sample replaced valid source");
    invalid->channels[1][123456]=0;
    invalid->channels[1].pop_back();
    check(!publishAsset(*plugin,invalid,"",false),"unequal channels reached publication");
    check(publishAsset(*plugin,{},"",false),"sample clear failed");
    publishCursorState(*plugin,nullptr,settings,true);
    check(readCursorSnapshot(*plugin,after) && after.asset==nullptr
        && after.rateA==0 && after.rateB==0,"cleared cursor state is stale");
    std::cout<<(ok?"PASS":"FAIL")<<": validated publication, no cursor PCM reads, stable cursor metadata and clear\n";
    return ok?0:1;
}
