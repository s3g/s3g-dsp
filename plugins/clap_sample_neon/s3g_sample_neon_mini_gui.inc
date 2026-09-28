    void paintMiniNeon() {
        const auto mode = static_cast<NeonMode>(p.visibleMode.load());
        const bool stack = mode == NeonMode::HotCue, capture = mode == NeonMode::HotLoop;
        const bool secondary = p.visibleLayer.load() != 0;
        const unsigned bank = p.visibleBank.load();
        const bool filling = p.fillHardwareHeld.load() || p.fillGuiHeld.load();
        const bool perform = mode == NeonMode::Sampler && !p.playEditEncoders.load();
        const unsigned page = mode == NeonMode::Sampler && p.playEditEncoders.load() && effectiveEditPage(p) != 10 ? effectiveEditPage(p) : 0;
        const bool fx = secondary && (mode == NeonMode::Sampler || (stack && effectiveEditPage(p) == 7));
        const char* layers[][2] {{"PLAY PADS","PAD FX"},{"SLICES","TOOLS"},{"LAYERS","EDIT TOOLS"},{"RECORD TOOLS","PLAY PADS"}};
        panel(rect(P::left-16, M::top, 302, M::bottom-M::top), stack
            ? format("NEON / STACK %c%zu / %c / %s", 'A'+selectedSlot/8, selectedSlot%8+1, 'A'+bank, layers[2][secondary])
            : format("NEON / BANK %c / %s", 'A'+bank, layers[static_cast<unsigned>(mode)][secondary]));
        const char* names[] {"PLAY","CHOP","STACK","RESAMPLE"};
        for (unsigned n=0; n<4; ++n) button(rect(M::pageLeft+n*(M::pageWidth+M::pageGap), M::pageTop, M::pageWidth, M::pageHeight),
            names[n], [this,n] { selectMode(n); }, static_cast<unsigned>(mode)==n);
        if (mode == NeonMode::Sampler) {
            text("ENCODERS",rect(P::left,M::encoderCenterY-9,78,18),style.label);
            menu(rect(M::encoderLeft,M::encoderTop,M::encoderWidth,M::encoderHeight),perform?"PERFORM":"EDIT",
                {"PERFORM","EDIT"},perform?0:1,[this](int n) {p.playEditEncoders.store(n==1);requestProcess(p);},!filling);
        } else text(stack ? !stackPlayable(p,selectedSlot) ? std::string("PRIMARY ONLY / SHIFT: EDIT") : format("LAYERS %02u-%02u / EDIT %02u",bank*8+1,bank*8+8,p.selectedLayers[selectedSlot].load()+1)
            : format("CELL %c%zu",'A'+selectedSlot/8,selectedSlot%8+1),rect(P::left,M::encoderCenterY-9,P::width,18),style.value);
        const auto slider = [&](const char* label, double y, double value, auto apply, auto reset, const std::string& valueLabel = "") {
            text(label,rect(P::left,y-9,78,18),style.label);
            foundation::drawHorizontalSlider(*context,rect(P::controlLeft,y-4,P::trackWidth,8),value,y-7,14,style);
            text(valueLabel.empty()?format("%.1f%%",value*100):valueLabel,rect(P::controlLeft+P::trackWidth+8,y-9,63,18),style.value,kRightText);
            drag(rect(P::controlLeft-4,y-11.5,P::trackWidth+8,23),[apply](CPoint at) {
                apply(std::clamp((at.x-P::controlLeft)/P::trackWidth,0.,1.));
            },{},{},reset);
        };
        const bool host = p.playbackClocks[selectedSlot].load();
        if (filling) {
            slider("TRAX BREAK",M::trax,p.fillBreakup.load(),[this](double v){p.fillBreakup.store(v);markStateDirty(p);requestProcess(p);},
                [this]{p.fillBreakup.store(.5f);markStateDirty(p);requestProcess(p);});
            text("LOOP REPEAT",rect(P::left,M::loop-9,78,18),style.label);
            fillRepeatMenu(rect(P::controlLeft,M::loop-10,P::controlWidth,20));
        } else if (stack) {
            if (stackPlayable(p,selectedSlot)) {
            const auto key = neonFamilyIndex(NeonFamily::LaneSlew);
            textureSliderRow(p.familyControls[selectedSlot][key],"TRAX FADE",M::trax-7,.001f,.1f,neonFamilyDef(key).initial,
                format("%.1f ms",1000*p.familyControls[selectedSlot][key].load()),
                [](double v){return v*1000;},[](double v){return v*.001;},P::left);
            const float target = p.stackTargets[selectedSlot].load();
            const unsigned layers=std::max(1u,stackCount(p,selectedSlot));
            const double position=target>=0?target:p.stackPositions[selectedSlot].load();
            slider("LOOP LAYER",M::loop,target>=0?target:p.stackPositions[selectedSlot].load(),
                [this](double v){queueStackManual(p,selectedSlot,v);},[this]{p.pendingStackResume.fetch_or(1u<<selectedSlot);requestProcess(p);},
                format("%.1f / %u",1+position*(layers-1),layers));
            }
        } else if (perform) {
            sliderRow(kGlobalMangleParamId,"TRAX MANGLE",M::trax-7,P::left);
            sliderRow(slotParamId(selectedSlot,kSlotGain),"LOOP GAIN",M::loop-7,P::left);
        } else if (page >= 4) {
            const char* trax[] {"REPEATS","TUNE","REPEATS","FX / P2","CYCLE","SPEED"};
            const char* loop[] {"STEP RATE","DURATION","CYCLES","FX / P1","EDIT LAYER","LAYER POS"};
            for (unsigned n=0;n<2;++n) {
                const double y=n?M::loop:M::trax;
                if (page == 7) {
                    const auto effect = fxEffect();
                    const auto key = s3g::sample::neonCharacterKnob(effect,p.fxPair.load(),n!=0,p.fxParameters[selectedSlot][1][7].load()>=.5f);
                    const char* label = key==12 ? "MIX" : s3g::sample::kNeonCharacterDefs[effect][key].label;
                    text(format("%s %s",n?"L":"T",label),rect(P::left,y-9,78,18),style.label);
                    const double v = advancedKnobValue(p,n!=0);
                    if (key<12 && *s3g::sample::kNeonCharacterDefs[effect][key].choices) {
                        const auto choices = fxChoices(s3g::sample::kNeonCharacterDefs[effect][key].choices);
                        const unsigned selected = static_cast<unsigned>(s3g::sample::neonCharacterDisplay(effect,key,v));
                        std::vector<bool> allowed;
                        for (unsigned choice=0;choice<choices.size();++choice) allowed.push_back(s3g::sample::neonCharacterControlAllowed(effect,key,choice,paramValue(p,slotParamId(selectedSlot,kSlotSourceFormat))>=.5));
                        menu(rect(P::controlLeft,y-12.5,P::controlWidth,25),choices[selected],choices,selected,
                            [this,effect,key,n](int choice){setAdvancedKnob(p,n!=0,s3g::sample::neonCharacterNormalized(effect,key,choice));},true,1,kCenterText,allowed);
                    } else {
                        foundation::drawHorizontalSlider(*context,rect(P::controlLeft,y-4,P::trackWidth,8),v,y-7,14,style);
                        text(key==12?format("%.1f%%",v*100):fxValue(effect,key,static_cast<float>(v)),
                            rect(P::controlLeft+P::trackWidth+I::valueGap,y-9,I::valueWidth,18),style.value,kRightText,font);
                        advancedKnobDrag(rect(P::controlLeft-4,y-11.5,P::trackWidth+8,23),P::controlLeft,P::trackWidth,n!=0);
                    }
                    continue;
                }
                text(format("%s %s",n?"L":"T",n?loop[page-4]:trax[page-4]),rect(P::left,y-9,78,18),style.label);
                const double v=advancedKnobValue(p,n!=0);
                foundation::drawHorizontalSlider(*context,rect(P::controlLeft,y-4,P::trackWidth,8),v,y-7,14,style);
                const auto valueText = n && page == 9 ? format("%.2f",1+v*(std::max(1u,stackCount(p,selectedSlot))-1))
                    : n && page == 8 ? format("%02u",1+static_cast<unsigned>(std::lround(v*31))) : format("%.1f%%",v*100);
                text(valueText,rect(P::controlLeft+P::trackWidth+8,y-9,63,18),style.value,kRightText);
                advancedKnobDrag(rect(P::controlLeft-4,y-11.5,P::trackWidth+8,23),P::controlLeft,P::trackWidth,n!=0);
            }
        } else {
            const double trax = page==2 ? host?nearestMotionCycleIndex(p.motionRates[selectedSlot].load())/7.:(p.motionSeconds[selectedSlot].load()-.05)/29.95
                : page==3 ? host?nearestMotionCycleIndex(p.grainIntervals[selectedSlot].load()*8)/7.:(p.grainDensities[selectedSlot].load()-1)/79.
                : std::log2(std::max(1.f,(capture?p.captureZoom:p.waveformZooms[selectedSlot]).load()))/5;
            const auto applyTrax=[this,page,host,capture](double v){
                if(page==2) {if(host)p.motionRates[selectedSlot].store(kMotionCycleBeats[std::lround(v*7)]);else p.motionSeconds[selectedSlot].store(.05+v*29.95);}
                else if(page==3) {if(host)p.grainIntervals[selectedSlot].store(kMotionCycleBeats[std::lround(v*7)]/8);else p.grainDensities[selectedSlot].store(1+v*79);}
                else (capture?p.captureZoom:p.waveformZooms[selectedSlot]).store(std::pow(2.,v*5));
                markStateDirty(p);requestProcess(p);
            };
            slider(page==2?"TRAX CYCLE":page==3?"TRAX RATE":"TRAX ZOOM",M::trax,trax,applyTrax,[applyTrax,page,host]{
                applyTrax(page==2?host?nearestMotionCycleIndex(ControlDefaults::cycleBeats)/7.:(ControlDefaults::cycleSeconds-.05)/29.95
                    :page==3?host?nearestMotionCycleIndex(ControlDefaults::grainInterval*8)/7.:(ControlDefaults::grainDensity-1)/79.:0.);
            });
            const auto applyLoop=[this,page,capture](double v){
                if(capture)p.captureCursor.store(v);else if(page==3)p.grainSizes[selectedSlot].store(5+v*495);else p.editPositions[selectedSlot].store(v);
                markStateDirty(p);requestProcess(p);
            };
            slider(page==3?"LOOP SIZE":page==2?"LOOP OFFSET":"LOOP CURSOR",M::loop,capture?p.captureCursor.load():page==3?(p.grainSizes[selectedSlot].load()-5)/495:p.editPositions[selectedSlot].load(),
                applyLoop,[applyLoop,page]{applyLoop(page==3?(ControlDefaults::grainSize-5)/495:0.);});
        }
        for(unsigned n=0;n<4;++n) {
            unsigned loaded=0;
            const auto* sources=p.publishedStacks[selectedSlot].load();
            for(unsigned pad=0;pad<8;++pad) loaded += stack ? sources&&n*8+pad<sources->count&&sources->layers[n*8+pad].asset
                : mode==NeonMode::Slicer ? n*8+pad<sliceCount(p,selectedSlot) : p.publishedAssets[n*8+pad].load()!=nullptr;
            button(rect(M::pageLeft+n*(M::pageWidth+M::pageGap),M::banks-10,M::pageWidth,20),
                format("%c %u/8",'A'+n,loaded),[this,n]{selectBank(n);},bank==n);
        }
        for(unsigned pad=0;pad<8;++pad) {
            const auto bounds=padRect(pad); const unsigned cell=bank*8+pad;
            const auto* sources=p.publishedStacks[selectedSlot].load();
            const bool layerPad=stack&&!secondary;
            const bool cellPad=mode==NeonMode::Sampler&&!secondary || capture&&secondary;
            const bool loaded=layerPad?sources&&cell<sources->count&&sources->layers[cell].asset:cellPad&&p.publishedAssets[cell].load();
            bool enabled=true,active=false;std::string label;
            if(layerPad){label=format("L%02u",cell+1);enabled=loaded&&stackPlayable(p,selectedSlot);active=cell==p.selectedLayers[selectedSlot].load();}
            else if(fx){label=upper(characterName(pad));enabled=fxAllowed(p,selectedSlot,pad);active=paramValue(p,slotParamId(selectedSlot,kSlotCharacter))==pad;}
            else if(mode==NeonMode::Slicer){const char* tools[]{"ADD","DELETE","AUDITION","ZERO","EQUAL","TRANS","ASSIGN 1","ASSIGN ALL"};label=secondary?tools[pad]:format("SL %02u",cell+1);enabled=bool(asset(selectedSlot))&&(secondary||cell<sliceCount(p,selectedSlot));active=!secondary&&cell==p.visibleSlice.load();}
            else if(stack){const char* tools[]{"SOURCE","SAMPLE","MOTION","GRAINS","ONE SHOT","HOLD","TOGGLE","STOP"};label=tools[pad];}
            else if(capture&&!secondary){const char* tools[]{"RECORD","STOP","AUDITION","DISCARD","BUS -","BUS +","REVIEW","ASSIGN"};label=tools[pad];active=pad==0&&p.captureState.load()==Plugin::CaptureState::Recording;}
            else {label=format("%c%u",'A'+bank,pad+1);active=selectedSlot==cell;}
            fill(bounds,active?style.buttonActive:loaded?color(0x353535):style.strip);stroke(bounds,active?style.text:style.grid,active?2:1);
            text(label,rect(bounds.left+2,bounds.top+2,bounds.getWidth()-4,16),enabled?style.text:style.dim,kCenterText);
            if(cellPad||layerPad){
                if(loaded)fill(rect(bounds.left+4,bounds.top+4,4,4),style.value);
                text(loaded?"AUDIO":"EMPTY",rect(bounds.left+2,bounds.top+17,bounds.getWidth()-4,12),loaded?style.value:style.dim,kCenterText,smallFont);
                const int playing=layerPad?p.stackWaveformLayers[selectedSlot].load():-1;
                const double position=playing==-2?p.stackPositions[selectedSlot].load()*std::max(1u,sources?unsigned(sources->count)-1:1u):playing;
                const double level=layerPad?playing>=-2&&playing!=-1?std::max(0.,1-std::abs(position-cell)):0:p.slotPeaks[cell].load();
                if(loaded&&level>.001)fill(rect(bounds.left+3,bounds.bottom-9,(bounds.getWidth()-6)*std::min(1.,level),2),style.value);
                if(cellPad) {
                    const int trigger=paramValue(p,slotParamId(cell,kSlotTriggerMode));
                    const bool generated=normalizedStageOptions(p.textureOptions[cell].load())!=0;
                    for(unsigned lamp=0;lamp<5;++lamp) {
                        const bool on=lamp<3 ? lamp==(trigger==3?1:trigger==1?2:0)
                            :lamp==3 ? generated ? trigger==3||trigger==1 : slotOptionEnabled(p,cell,kSlotRepeatOption)||paramValue(p,slotParamId(cell,kSlotDirection))>=2
                            :generated ? (playbackIndex(p,cell)!=5||p.sourceModes[cell].load()==4)&&p.playbackClocks[cell].load()!=0 : slotOptionEnabled(p,cell,kSlotSyncOption);
                        fill(rect(bounds.left+5+lamp*11,bounds.bottom-4,8,2),loaded&&on?style.fill:style.cell);
                    }
                }
            }
            if(!enabled)continue;
            if(layerPad) drag(bounds,[](CPoint){},[this,pad,cell,slot=selectedSlot]{p.pendingStackGui[pad].store(1+(slot<<5u)+cell);requestProcess(p);},
                [this,pad]{p.pendingStackGuiReleases.fetch_or(1u<<pad);requestProcess(p);});
            else if(fx) drag(bounds,[](CPoint){},[this,pad,slot=selectedSlot]{queueGuiParamGesture(p,slotParamId(slot,kSlotCharacter),pad);p.pendingGuiFx.store((slot<<1u)|1u);requestProcess(p);},
                [this,slot=selectedSlot]{p.pendingGuiFx.store(slot<<1u);requestProcess(p);});
            else if(cellPad && (!capture||loaded)) drag(bounds,[](CPoint){},[this,cell]{p.selectedSlot.store(cell);auditionSlot(p,cell);},
                [this,cell]{p.pendingGuiReleases.fetch_or(1u<<cell);requestProcess(p);});
            else hit(bounds,[this,mode,secondary,pad,cell]{
                if(mode==NeonMode::Slicer){if(secondary)chopToolAction(p,pad);else auditionSlice(p,selectedSlot,cell);}
                else if(mode==NeonMode::HotLoop)capturePadAction(p,pad,secondary);
                else {editToolAction(p,pad);if(pad!=7)auditionEditedSlot(p,selectedSlot);}
            });
        }
        button(rect(P::columnLeft(0,3),M::actions-10,P::columnWidth(3),20),capture?(secondary?"REC TOOLS":"PLAY PADS"):secondary?"PRIMARY":"SECOND",
            [this,mode,secondary]{selectSurface(p,mode,secondary?0:1);},secondary);
        button(rect(P::columnLeft(1,3),M::actions-10,P::columnWidth(3),20),"KILL",[this]{p.killRequested.store(true);p.captureAudition.store(false);requestProcess(p);});
        button(rect(P::columnLeft(2,3),M::actions-10,P::columnWidth(3),20),"RESUME",[this]{p.pendingStackResume.fetch_or(1u<<selectedSlot);requestProcess(p);},false,stack);
        fillHoldButton(rect(P::columnLeft(0,2),M::fill-10,P::columnWidth(2),20));
        button(rect(P::columnLeft(1,2),M::fill-10,P::columnWidth(2),20),"FILL SET",[this]{p.fillPage.store(!p.fillPage.load());},p.fillPage.load());
        sliderRow(kMasterGainParamId,"MASTER",M::master-7,P::left);
        if(p.captureState.load()==Plugin::CaptureState::Recording)button(rect(P::left,M::capture-10,P::width,20),"STOP CAPTURE",[this]{capturePadAction(p,1,false);},true);
    }
