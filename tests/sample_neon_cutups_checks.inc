void testCutupsPlayback() {
    using namespace s3g::sample;
    using F = NeonFamily;
    // The 32-lane specialization must render the exact four-file algorithm,
    // including its random stream, repeats, gate, joins and voice envelope.
    std::array<SampleAsset,32> assets;
    for (unsigned n=0;n<32;++n) assets[n]=constantAsset(.01f*(n+1),-.005f*(n+1),4096);
    SampleCutupsEngine original;
    BasicSampleCutupsEngine<32> expanded;
    original.prepare(48000,2); expanded.prepare(48000,2);
    for (unsigned n=0;n<4;++n) { original.setAsset(n,&assets[n]); expanded.setAsset(n,&assets[n]); }
    SampleCutupsSettings a;
    BasicSampleCutupsSettings<32> b;
    a.clockBasis=b.clockBasis=CutClockBasis::Hertz;
    a.cutRateHz=b.cutRateHz=80;
    a.swing=b.swing=.3f; a.timeVariation=b.timeVariation=.2f;
    a.reverseChance=b.reverseChance=.4f; a.pitchVariationSemitones=b.pitchVariationSemitones=7;
    a.levelVariation=b.levelVariation=.3f; a.repeatCount=b.repeatCount=2;
    b.manualPattern=a.manualPattern;
    std::array<float,64> al{},ar{},bl{},br{};
    CutupsRenderEvent note; note.noteId=42; note.velocity=.7f;
    bool identical=true;
    for (unsigned file=0;file<10;++file) for (unsigned source=0;source<7;++source) {
        a.fileOrder=b.fileOrder=static_cast<CutFileOrder>(file);
        a.sourceOrder=b.sourceOrder=static_cast<CutSourceOrder>(source);
        original.reset(); expanded.reset();
        for (unsigned block=0;block<100;++block) {
            original.render(a,block?nullptr:&note,block?0:1,al.data(),ar.data(),64);
            expanded.render(b,block?nullptr:&note,block?0:1,bl.data(),br.data(),64);
            identical &= al==bl && ar==br;
        }
    }
    check(identical,"Cutups four-lane specialization exact sample parity across all file/source orders");
    // Loaded count, not capacity, determines traversal. Include non-powers
    // of two and prove that 32 does not silently wrap at lane four.
    std::array<float,600> left{},right{};
    for (unsigned count : {1u,2u,3u,4u,8u,16u,32u}) for (unsigned order : {0u,1u,4u,6u,7u,8u,9u}) {
        expanded.reset(); b=BasicSampleCutupsSettings<32>{};
        b.clockBasis=CutClockBasis::Hertz; b.cutRateHz=80;
        b.fileOrder=static_cast<CutFileOrder>(order); b.patternLength=64; b.regionCount=1;
        for (unsigned n=0;n<32;++n) expanded.setPreparedAsset(n,n<count?&assets[n]:nullptr);
        uint32_t visited=0; bool bounds=true;
        for (unsigned cut=0;cut<64;++cut) {
            // Inspect inside each gate, not exactly at its silent end frame.
            expanded.render(b,cut?nullptr:&note,cut?0:1,left.data(),right.data(),cut?600:599);
            const auto lane=expanded.voiceCursors()[0].lane;
            bounds &= expanded.voiceCursorCount()==1 && lane<count;
            visited |= 1u<<lane;
        }
        const uint32_t expected=count==32?UINT32_MAX:(1u<<count)-1;
        check(bounds && visited==expected,"Cutups visits every loaded layer for small and full stacks");
    }
    // Neon adapter vs the shared engine, with distinct edits/BPM for each
    // source. Full-field anti-phase channel relationships must remain intact.
    auto engine=std::make_unique<SampleNeonEngine>();
    check(engine->prepare(48000,64),"Neon Cutups prepare");
    NeonStack stack; stack.count=3;
    for (unsigned n=0;n<3;++n) {
        assets[n].channelCount=16;
        for (unsigned ch=0;ch<16;++ch) {
            assets[n].channels[ch].resize(4096);
            for (unsigned i=0;i<4096;++i)
                assets[n].channels[ch][i]=float(.02*(n+1)*(ch%2?-.5:1)*std::sin(i*.13));
        }
        stack.layers[n]={&assets[n],.1*n,.7+.1*n}; stack.sourceBpms[n]=90.f+n*30.f;
        stack.cutupsMetadata[n].transientRegions.count=3;
        stack.cutupsMetadata[n].transientRegions.starts={0,.3f,.6f};
    }
    engine->setAsset(0,&assets[0]);
    SampleNeonSettings settings; settings.masterGainDecibels=0;
    settings.outputLayout=SampleNeonOutputLayout::Ambisonic3;
    auto& s=settings.slots[0]; s.stack=&stack; s.playback=SampleNeonPlayback::Cutups;
    s.sourceFormat=SampleNeonSourceFormat::Ambisonic; s.triggerMode=TriggerMode::Gate;
    s.gainDecibels=0; s.start=0; s.end=.7;
    s.family[F::CutRate]=80; s.family[F::CutRegions]=32;
    s.family[F::CutReverse]=.3f; s.family[F::CutPitchVariation]=5;
    s.family[F::CutRegionMode]=1;
    expanded.prepare(48000,16); b=BasicSampleCutupsSettings<32>{};
    b.clockBasis=CutClockBasis::Hertz; b.cutRateHz=80;
    b.patternLength=b.regionCount=32; b.reverseChance=.3f; b.pitchVariationSemitones=5;
    b.regionMode=CutRegionMode::Transient; b.outputGainDecibels=0; b.useLaneWindows=true;
    for (unsigned n=0;n<32;++n) {
        expanded.setPreparedAsset(n,n<3?&assets[n]:nullptr);
        expanded.setPreparedMetadata(n,n<3?&stack.cutupsMetadata[n]:nullptr);
        if(n<3) { b.laneStarts[n]=stack.layers[n].start; b.laneEnds[n]=stack.layers[n].end; b.laneBpm[n]=stack.sourceBpms[n]; }
    }
    OutputBlock out,ref;
    SampleNeonEvent on; on.noteId=42; on.value=.7f;
    identical=true; bool coherent=true; uint32_t visited=0;
    for (unsigned block=0;block<1000;++block) {
        engine->render(settings,block?nullptr:&on,block?0:1,out.pointers.data(),32,64);
        expanded.render(b,block?nullptr:&note,block?0:1,ref.pointers.data(),16,64);
        for (unsigned ch=0;ch<16;++ch) for (unsigned i=0;i<64;++i) {
            identical &= std::abs(out.samples[ch][i]-ref.samples[ch][i])<1.e-7f;
            coherent &= std::abs(out.samples[ch][i]-out.samples[0][i]*(ch%2?-.5f:1.f))<1.e-7f;
        }
        if (engine->voiceCursorCount(0)) visited |= 1u<<engine->voiceCursors(0)[0].layer;
    }
    check(identical,"Neon Cutups uses shared engine sample-for-sample with per-layer trims/transients");
    check(coherent && visited==7,"Neon Cutups preserves ACN/SN3D channel coherence and publishes actual stack layer");
    auto off=on; off.kind=SampleNeonEventKind::Release;
    engine->render(settings,&off,1,out.pointers.data(),32,64);
    for (unsigned n=0;n<32;++n) engine->render(settings,nullptr,0,out.pointers.data(),32,64);
    check(!engine->slotPlaybackActive(0),"Cutups Hold release retires its voice");
    for (auto trigger : {TriggerMode::OneShot,TriggerMode::Toggle}) {
        engine->reset(); s.triggerMode=trigger; s.family[F::CutRegions]=2;
        engine->render(settings,&on,1,out.pointers.data(),32,64);
        if (trigger==TriggerMode::Toggle) { ++on.noteId; engine->render(settings,&on,1,out.pointers.data(),32,64); }
        for (unsigned n=0;n<64;++n) engine->render(settings,nullptr,0,out.pointers.data(),32,64);
        check(!engine->slotPlaybackActive(0),"Cutups One Shot completes one pattern; Toggle handles fresh Neon note IDs");
    }
    engine->reset(); s.triggerMode=TriggerMode::Gate;
    engine->render(settings,&on,1,out.pointers.data(),32,64);
    engine->killAll(); engine->render(settings,nullptr,0,out.pointers.data(),32,64);
    check(!engine->slotPlaybackActive(0) && engine->outputPeak()==0,"Cutups KILL clears voices and output");
}
