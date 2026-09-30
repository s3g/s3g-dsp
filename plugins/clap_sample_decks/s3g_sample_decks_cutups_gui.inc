    // Neon's pattern strip: one vertical layer slider per active step, with
    // the source-position slider overlaid in dim gray (Option/Alt-drag).
    std::array<unsigned,2> cutEditStep {};
    bool cutPatternDrag=false,cutPatternSourceDrag=false;
    unsigned cutDragDeck=0;
    std::array<bool,64> cutGestures {};
    CRect cutPatternBounds(unsigned d) const {return rect(left(1-d)+16,176,panelWidth-32,336);}
    unsigned cutPatternLayer(unsigned d,unsigned step,const NeonFamilySettings& f) const {
        std::array<unsigned,32> loaded {};unsigned count=0;
        for(unsigned n=0;n<32;++n)if(p.document->decks[d].layers[n].asset)loaded[count++]=n;
        if(!count)return 0;
        const auto order=static_cast<CutFileOrder>(f[F::CutFileOrder]);
        if(order!=CutFileOrder::Manual)
            return loaded[cutupsFileOrderIndex(order,step,count,static_cast<unsigned>(f[F::CutSeed]))];
        const unsigned requested=static_cast<unsigned>(f.values[neonFamilyIndex(F::CutPatternLane)+step]);
        unsigned nearest=loaded[0];
        for(unsigned n=1;n<count;++n)if(std::abs(int(loaded[n])-int(requested))<std::abs(int(nearest)-int(requested)))nearest=loaded[n];
        return nearest;
    }
    void cutPatternValue(unsigned d,unsigned key,double v){
        const unsigned id=param(d,key);if(value(p,id)==v)return;
        gesture(id,true);set(id,v);gesture(id,false);
    }
    void manualCutPattern(unsigned d,bool source){
        if(source){cutPatternValue(d,family(F::CutSourceOrder),6);return;}
        if(control(p,d,family(F::CutFileOrder))==5)return;
        const auto f=pathSettings(d);
        // Capture every step, including those beyond the current length, so
        // the first edit changes only the touched layer in the generated order.
        for(unsigned n=0;n<64;++n)cutPatternValue(d,family(F::CutPatternLane)+n,cutPatternLayer(d,n,f));
        cutPatternValue(d,family(F::CutFileOrder),5);
    }
    void editCutPatternAt(CPoint point,bool reset=false){
        const unsigned d=cutDragDeck,steps=static_cast<unsigned>(control(p,d,family(F::CutRegions)));
        const unsigned layers=std::max(1u,unsigned(p.document->decks[d].stack.count));
        const auto area=cutPatternBounds(d);
        const unsigned step=std::min(steps-1,static_cast<unsigned>(std::clamp((point.x-area.left)/area.getWidth(),0.,1.)*steps));
        cutEditStep[d]=step;
        manualCutPattern(d,cutPatternSourceDrag);
        const unsigned id=param(d,family(cutPatternSourceDrag?F::CutPatternSource:F::CutPatternLane)+step);
        if(!cutGestures[step]){gesture(id,true);cutGestures[step]=true;}
        const double y=std::clamp((point.y-area.top)/area.getHeight(),0.,1.);
        set(id,reset?p.defs[id].initial:cutPatternSourceDrag?y:std::min(layers-1,static_cast<unsigned>(y*layers)));
        invalid();
    }
    void endCutPatternDrag(){
        for(unsigned n=0;n<64;++n)if(cutGestures[n]){
            gesture(param(cutDragDeck,family(cutPatternSourceDrag?F::CutPatternSource:F::CutPatternLane)+n),false);cutGestures[n]=false;}
        cutPatternDrag=false;
    }
    void cutPattern(unsigned d){
        const double x=left(1-d);const auto area=cutPatternBounds(d);const auto f=pathSettings(d);
        const unsigned steps=static_cast<unsigned>(f[F::CutRegions]),layers=std::max(1u,unsigned(p.document->decks[d].stack.count));
        cutEditStep[d]=std::min(cutEditStep[d],steps-1);
        controlRow(param(d,family(F::CutSourceOrder)),x+16,104);
        controlRow(param(d,family(F::CutRegions)),x+312,104,276,"PATTERN LENGTH");
        text(format("STEP %02u / LAYER %02u / SRC %.1f%%",cutEditStep[d]+1,cutPatternLayer(d,cutEditStep[d],f)+1,
            f.values[neonFamilyIndex(F::CutPatternSource)+cutEditStep[d]]*100),rect(x+16,130,286,18),style.value);
        text("DRAG: LAYER / OPTION-DRAG: SOURCE",rect(x+312,130,276,18),style.label,kRightText);
        fill(area,color(0x171717));
        const double w=area.getWidth()/steps;
        fill(rect(area.left+w*cutEditStep[d],area.top,w,area.getHeight()),style.cell);
        for(unsigned n=1;n<layers;++n){const double y=area.top+area.getHeight()*n/layers;
            line({area.left,y},{area.right,y},color(0x303030),.5);}
        for(unsigned n=0;n<steps;++n){
            const double xx=area.left+w*n,y=area.top+area.getHeight()*(cutPatternLayer(d,n,f)+.5)/layers;
            line({xx,area.top},{xx,area.bottom},color(0x424242),.5);
            const double sy=area.top+f.values[neonFamilyIndex(F::CutPatternSource)+n]*area.getHeight();
            line({xx+1,sy},{xx+w-1,sy},color(0x747474),1);
            line({xx+1,y},{xx+w-1,y},style.text,2);
            const unsigned labelEvery=steps>32?4:steps>16?2:1;
            if(n==cutEditStep[d]||(n%labelEvery==0&&std::abs(int(n)-int(cutEditStep[d]))>=int(labelEvery)))
                text(format("%02u",n+1),rect(xx+w*.5-12,154,24,18),n==cutEditStep[d]?style.text:style.dim,kCenterText);
        }
        for(unsigned n=0;n<visual[d].count;++n){const auto& cursor=visual[d].cursors[n];
            if(control(p,d,Playback)!=8||cursor.layer>=layers)continue;
            const double xx=area.left+std::clamp(double(cursor.windowPhase)+.5/steps,0.,1.)*area.getWidth();
            const double y=area.top+area.getHeight()*(cursor.layer+.5)/layers;
            line({xx,area.top},{xx,area.bottom},color(0x9a9a9a),.75);ellipse(rect(xx-2.5,y-2.5,5,5),style.text);
        }
        stroke(area,style.grid);
    }
