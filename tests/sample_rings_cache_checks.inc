// Included after the real SampleFamilyView and Fixture declarations.
// Compare the cached path with the unchanged uncached drawing fallback.
bool ringsCacheChecks(bool benchmark)
{
    using namespace VSTGUI;
    using namespace s3g::portable_gui;
    Fixture f;
    f.add("Selected Slot",0,3);
    for(unsigned slot=0;slot<4;++slot) {
        const auto name=std::string("Slot ")+char('A'+slot)+" Wrap Nudge";
        f.add(name.c_str(),0);
    }
    std::array<std::shared_ptr<const s3g::sample::SampleAsset>,4> assets;
    const auto makeAsset=[](unsigned channels, float phase, unsigned frames=32768) {
        auto result=std::make_shared<s3g::sample::SampleAsset>();
        result->channelCount=static_cast<uint8_t>(channels);
        for(unsigned ch=0;ch<channels;++ch) {
            result->channels[ch].resize(frames);
            for(unsigned i=0;i<frames;++i)
                result->channels[ch][i]=.3f*std::sin(float(i)*.021f+float(ch)+phase)
                    +.2f*std::cos(float(i)*.0173f+phase);
        }
        return result;
    };
    assets[0]=makeAsset(1,0); assets[1]=makeAsset(2,.6f); assets[3]=makeAsset(8,1.3f);
    auto uncachedConfig=f.config(SampleFamilyVisualization::Rings);
    // The context for these two callbacks is separate from parameter callbacks.
    // Use a fixture-derived owner so all callbacks retain the same context.
    struct OwnedFixture : Fixture {
        decltype(assets)* sources=nullptr;
    } ownedFixture;
    static_cast<Fixture&>(ownedFixture)=f;
    ownedFixture.sources=&assets;
    uncachedConfig=ownedFixture.config(SampleFamilyVisualization::Rings);
    uncachedConfig.callbacks.getAsset=[](void* p,uint32_t slot) {
        const auto& a=*static_cast<OwnedFixture*>(static_cast<Fixture*>(p))->sources;
        return slot<a.size()?a[slot].get():nullptr;
    };
    auto cachedConfig=uncachedConfig;
    cachedConfig.callbacks.getOwnedAsset=[](void* p,uint32_t slot) {
        const auto& a=*static_cast<OwnedFixture*>(static_cast<Fixture*>(p))->sources;
        return slot<a.size()?a[slot]:std::shared_ptr<const s3g::sample::SampleAsset>{};
    };
    SampleFamilyView reference(uncachedConfig), cached(cachedConfig);
    uint64_t comparisons=0;
    const auto compare=[&](double zoom,double dpi) {
        const CPoint size(1356*zoom,844*zoom);
        auto a=COffscreenContext::create(size,dpi), b=COffscreenContext::create(size,dpi);
        if(!a||!b) return false;
        const auto draw=[&](COffscreenContext& context,SampleFamilyView& view) {
            context.beginDraw();
            { CDrawContext::Transform transform(context,CGraphicsTransform().scale(zoom,zoom)); view.draw(&context); }
            context.endDraw();
        };
        draw(*a,reference); draw(*b,cached);
        auto pa=owned(CBitmapPixelAccess::create(a->getBitmap()));
        auto pb=owned(CBitmapPixelAccess::create(b->getBitmap()));
        if(!pa||!pb||pa->getBitmapWidth()!=pb->getBitmapWidth()
            ||pa->getBitmapHeight()!=pb->getBitmapHeight()) return false;
        uint64_t differences=0;
        do {
            uint32_t va,vb; pa->getValue(va); pb->getValue(vb);
            ++comparisons; if(va!=vb) ++differences;
            ++*pb;
        } while(++*pa);
        if(differences) std::cerr<<"Rings cache raster differences: "<<differences
            <<" zoom="<<zoom<<" dpi="<<dpi<<'\n';
        return differences==0;
    };
    if(!compare(1,1)||!compare(1,1)||!compare(.65,1)||!compare(2,1)||!compare(1,2)) return false;
    ownedFixture.values[1]=.273;
    if(!compare(1,1)) return false; // wrap-nudge invalidates geometry
    std::weak_ptr<const s3g::sample::SampleAsset> old=assets[0];
    assets[0]=makeAsset(8,2.1f);
    if(old.expired()||!compare(1,1)||!old.expired()) {
        std::cerr<<"Rings source replacement did not retain then release its cached owner\n"; return false;
    }
    click(reference,470,702); click(cached,470,702);
    if(!compare(1,1)) return false; // selected slot/color change
    assets[1].reset(); assets[3].reset();
    if(!compare(1,1)) return false; // fewer rings changes every radius/pitch
    assets[0].reset();
    if(!compare(1,1)) return false;
    auto invalid=makeAsset(2,0);
    invalid->channels[1][10]=std::numeric_limits<float>::quiet_NaN();
    assets[2]=invalid;
    if(!compare(1,1)) return false; // invalid source still rejected
    assets[2]=makeAsset(2,.3f);
    if(!compare(1,1)) return false;
    if(benchmark) {
        assets[2]=makeAsset(8,.3f,1024*1024);
        if(!compare(1,1)) return false;
        auto context=COffscreenContext::create(CPoint(1356,844));
        if(!context) return false;
        using Clock=std::chrono::steady_clock;
        std::vector<double> times[2];
        for(unsigned n=0;n<60;++n) for(unsigned execution=0;execution<2;++execution) {
            const unsigned side=(n+execution)&1;
            const auto start=Clock::now();
            context->beginDraw();
            (side==0?reference:cached).draw(context);
            context->endDraw();
            const auto end=Clock::now();
            if(n>=10) times[side].push_back(std::chrono::duration<double,std::micro>(end-start).count());
        }
        for(auto& t:times) std::sort(t.begin(),t.end());
        std::cout<<"Rings full redraw, 8-channel 1048576-frame source: reference_us="<<times[0][25]
            <<" optimized_us="<<times[1][25]<<'\n';
        if(!compare(1,1)) return false;
    }
    std::cout<<"Rings cache: "<<comparisons<<" identical pixels across reuse, resize/DPI, nudge, ownership, replacement, clear and invalid assets\n";
    return true;
}
