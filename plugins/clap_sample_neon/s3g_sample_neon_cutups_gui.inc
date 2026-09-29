    unsigned cutEditStep = 0;
    bool cutPatternDrag = false, cutPatternSourceDrag = false;
    std::size_t cutPatternSlot = 0;
    bool cutupsPatternVisible() const {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        return playbackChoice() == 8 && (mode == NeonMode::Sampler || mode == NeonMode::HotCue);
    }
    unsigned cutPatternLayer(unsigned step, const NeonFamilySettings& f) const {
        std::array<unsigned,32> loaded {}; unsigned count = 0;
        for (unsigned n = 0; n < stackCount(p,selectedSlot); ++n)
            if (p.sources[selectedSlot][n].asset) loaded[count++] = n;
        if (!count) return 0;
        const auto order = static_cast<s3g::sample::CutFileOrder>(f[NeonFamily::CutFileOrder]);
        if (order != s3g::sample::CutFileOrder::Manual)
            return loaded[s3g::sample::cutupsFileOrderIndex(order,step,count,static_cast<unsigned>(f[NeonFamily::CutSeed]))];
        const unsigned requested = static_cast<unsigned>(f.values[neonFamilyIndex(NeonFamily::CutPatternLane)+step]);
        unsigned nearest = loaded[0];
        for (unsigned n = 1; n < count; ++n)
            if (std::abs(int(loaded[n])-int(requested)) < std::abs(int(nearest)-int(requested))) nearest = loaded[n];
        return nearest;
    }
    void editCutPatternAt(CPoint point) {
        using F = NeonFamily;
        const auto area = stackPathRect();
        const auto steps = static_cast<unsigned>(familyValue(F::CutRegions).load());
        const unsigned layers = std::max(1u,stackCount(p,selectedSlot));
        cutEditStep = std::min(steps-1,static_cast<unsigned>(std::clamp((point.x-area.left)/area.getWidth(),0.,1.)*steps));
        const float y = static_cast<float>(std::clamp((point.y-area.top)/area.getHeight(),0.,1.));
        if (cutPatternSourceDrag) {
            familyValue(static_cast<F>(neonFamilyIndex(F::CutPatternSource)+cutEditStep)).store(y);
            familyValue(F::CutSourceOrder).store(6);
        } else {
            // First manual edit preserves the displayed generated order.
            if (familyValue(F::CutFileOrder).load() != 5) {
                const auto f = familySnapshot();
                for (unsigned n=0;n<64;++n) familyValue(static_cast<F>(neonFamilyIndex(F::CutPatternLane)+n)).store(static_cast<float>(cutPatternLayer(n,f)));
            }
            familyValue(static_cast<F>(neonFamilyIndex(F::CutPatternLane)+cutEditStep)).store(static_cast<float>(std::min(layers-1,static_cast<unsigned>(y*layers))));
            familyValue(F::CutFileOrder).store(5);
        }
        markStateDirty(p); requestProcess(p); invalid();
    }
    void paintCutupsPattern() {
        using F = NeonFamily;
        const auto area = stackPathRect(); const auto f = familySnapshot();
        const auto steps = static_cast<unsigned>(f[F::CutRegions]);
        const unsigned layers = std::max(1u,stackCount(p,selectedSlot));
        fill(area,color(0x171717));
        for (unsigned n = 0; n < steps; ++n) {
            const double x = area.left+area.getWidth()*n/steps, w = area.getWidth()/steps;
            const auto layer = cutPatternLayer(n,f);
            const double y = area.top+area.getHeight()*(layer+.5)/layers;
            if (n == cutEditStep) fill(rect(x,area.top,w,area.getHeight()),color(0x272727));
            line({x,area.top},{x,area.bottom},style.grid,.5);
            line({x+1,y},{x+w-1,y},style.text,2);
            const double sy = area.top+f.values[neonFamilyIndex(F::CutPatternSource)+n]*area.getHeight();
            line({x+1,sy},{x+w-1,sy},color(0x747474),.75);
        }
        for (unsigned n=0;n<visual.count;++n) {
            const auto& cursor = visual.cursors[n];
            if (cursor.layer >= layers) continue;
            const double x = area.left+area.getWidth()*(cursor.windowPhase+.5/steps);
            const double y = area.top+area.getHeight()*(cursor.layer+.5)/layers;
            line({x,area.top},{x,area.bottom},color(0x9a9a9a),.75);
            ellipse(rect(x-2.5,y-2.5,5,5),style.text);
        }
        stroke(area,style.grid);
    }
    void paintCutupsPlayback() {
        using F = NeonFamily;
        const bool host = p.playbackClocks[selectedSlot].load() != 0;
        inspectorLabel<E>("CLOCK",5);
        menu(inspectorMenu<E>(5),host ? "HOST TEMPO" : "FREE / PAD",{"FREE / PAD","HOST TEMPO"},host?1:0,
            [this](int n) { p.playbackClocks[selectedSlot].store(static_cast<uint8_t>(n)); markStateDirty(p); requestProcess(p); });
        if (host) familyMenu<E>(F::CutDivision,"DIVISION",6,{"1/1","1/2","1/4","1/8","1/16","1/32","1/8 TRIPLET","1/16 TRIPLET"});
        else familySlider<E>(F::CutRate,"CUT RATE",6," Hz");
        familyMenu<E>(F::CutRegionMode,"REGIONS",7,{"EQUAL","TRANSIENT"});
        familySlider<E>(F::CutRegions,"STEPS/REG",8);
        familyMenu<E>(F::CutSourceOrder,"SRC ORDER",9,{"TIMELINE","FORWARD","REVERSE","PALINDROME","RANDOM","WALK","MANUAL"});
        familySlider<E>(F::CutRepeat,"REPEAT",10);
        familySlider<E>(F::CutGate,"GATE",11);
        familySlider<E>(F::CutJoin,"JOIN",12," ms");
        familySlider<E>(F::CutSwing,"SWING",13);
        familySlider<E>(F::CutTimeVariation,"TIME VAR",14);
        familySlider<E>(F::CutReverse,"REVERSE",15);
        familySlider<E>(F::CutPitchVariation,"PITCH VAR",16," st");
        familySlider<E>(F::CutLevelVariation,"LEVEL VAR",17);
        familyMenu<E>(F::CutTempoSync,"TEMPO SYNC",18,{"OFF","VARISPEED"});
        familyMenu<E>(F::CutVoiceMode,"VOICES",19,{"POLY","MONO","LEGATO"});
        familyMenu<E>(F::CutPolyPath,"POLY PATH",20,{"TOGETHER","STEP OFFSET","QUARTER SPREAD","MIRROR PAIRS"});
        for (unsigned n=0;n<2;++n) {
            const auto key = n ? F::CutRelease : F::CutAttack;
            const auto d = neonFamilyDef(neonFamilyIndex(key)); auto& value = familyValue(key);
            textureSliderRow(value,n?"RELEASE":"ATTACK",E::slider(21+n),d.minimum,d.maximum,d.initial,format("%.3f s",value.load()));
        }
        familySlider<E>(F::CutSeed,"SEED",23);
        if (paramValue(p,slotParamId(selectedSlot,kSlotSourceFormat)) < .5 && familyValue(F::RoutingMode).load() != 0)
            familyMenu<E>(F::CutAllocation,"ALLOCATE",24,{"PER NOTE","PER CUT","PER PATTERN"});
        else inspectorReadout<E>("ALLOCATION","PRESERVE FIELD",24);
        const unsigned steps = static_cast<unsigned>(familyValue(F::CutRegions).load());
        cutEditStep = std::min(cutEditStep,steps-1);
        std::vector<std::string> names;
        for (unsigned n = 0; n < steps; ++n) names.push_back(format("%02u",n+1));
        inspectorLabel<E>("EDIT STEP",25);
        menu(inspectorMenu<E>(25),names[cutEditStep],names,static_cast<int>(cutEditStep),[this](int n){cutEditStep=n;},true,4);
        const auto laneKey = static_cast<F>(neonFamilyIndex(F::CutPatternLane)+cutEditStep);
        const unsigned layers = std::max(1u,stackCount(p,selectedSlot));
        names.clear(); for (unsigned n=0;n<layers;++n) names.push_back(format("LAYER %02u",n+1));
        const auto lane = std::min(layers-1,static_cast<unsigned>(familyValue(laneKey).load()));
        inspectorLabel<E>("STEP LAYER",26);
        menu(inspectorMenu<E>(26),names[lane],names,static_cast<int>(lane),[this,laneKey](int n) {
            familyValue(laneKey).store(static_cast<float>(n)); familyValue(NeonFamily::CutFileOrder).store(5);
            markStateDirty(p); requestProcess(p);
        },stackCount(p,selectedSlot)>0,4);
        const auto key = static_cast<F>(neonFamilyIndex(F::CutPatternSource)+cutEditStep);
        auto& source = familyValue(key);
        textureSliderRow(source,"STEP SRC",E::slider(27),0,1,neonFamilyDef(neonFamilyIndex(key)).initial,
            format("%.1f%%",source.load()*100));
        textureDoubleSliderRow(p.sourceBpms[selectedSlot],"LAYER BPM",E::slider(28),20,400,120,
            format("L%02u %.1f",p.selectedLayers[selectedSlot].load()+1,p.sourceBpms[selectedSlot].load()));
    }
