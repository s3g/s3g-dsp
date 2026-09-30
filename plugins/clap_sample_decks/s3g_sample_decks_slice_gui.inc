    bool sliceDrag=false;
    unsigned sliceDeck=0,sliceLayer=0,sliceMarker=0;
    SampleNeonSliceLayout sliceDraft;
    std::shared_ptr<const Document> sliceDocument;
    DeckViewport sliceViewport;
    double slicePosition(unsigned d,CPoint point,const DeckViewport& v) const {
        const auto b=wave(d);const double fraction=p.viewState.vertical(d)?(point.y-b.top)/b.getHeight():(point.x-b.left)/b.getWidth();
        const auto& l=p.document->decks[d].layers[layer(d)];
        return (v.at(fraction)-l.start)/std::max(1.e-9,l.end-l.start);
    }
    void setSliceLayout(unsigned d,const SampleNeonSliceLayout& layout) {
        if(blocked()||!layout.valid())return;
        const auto& old=p.document->decks[d].layers[layer(d)];
        if(!old.asset||(old.slices.sliceCount==layout.sliceCount&&old.slices.boundaries==layout.boundaries))return;
        auto doc=std::make_shared<Document>(*p.document);doc->decks[d].layers[layer(d)].slices=layout;
        doc->action="EDIT SLICE MARKERS";publish(p,std::move(doc),1u<<d);guiValue(p,param(d,SliceCount),layout.sliceCount);
        p.message="SLICE MARKERS UPDATED / UNDO TO RESTORE";
    }
    bool sliceDown(unsigned d,MouseDownEvent& e) {
        if(blocked())return true;const auto& l=p.document->decks[d].layers[layer(d)];if(!l.asset)return true;
        const auto v=viewport(d);const double position=slicePosition(d,e.mousePosition,v);
        const double extent=p.viewState.vertical(d)?wave(d).getHeight():wave(d).getWidth();
        unsigned nearest=0;double distance=7*v.width/extent/std::max(1.e-9,l.end-l.start);
        for(unsigned i=1;i<l.slices.sliceCount;++i)if(const double delta=std::abs(position-l.slices.boundaries[i]);delta<distance){distance=delta;nearest=i;}
        if(e.buttonState.isRight()){auto slices=l.slices;if(removeDeckSliceMarker(slices,nearest))setSliceLayout(d,slices);return true;}
        if(!e.buttonState.isLeft())return false;
        sliceDraft=l.slices;
        if(!nearest){if(!addSampleNeonSliceMarker(sliceDraft,position)){p.message="CLICK INSIDE THE TRIM / MAXIMUM 32 SLICES";return true;}
            for(unsigned i=1;i<sliceDraft.sliceCount;++i)if(sliceDraft.boundaries[i]==position){nearest=i;break;}}
        sliceDocument=p.document;sliceDeck=d;sliceLayer=layer(d);sliceMarker=nearest;sliceViewport=v;sliceDrag=true;
        moveSlice(e.mousePosition);return true;
    }
    void moveSlice(CPoint point) {
        if(!sliceDrag)return;
        if(p.document!=sliceDocument||layer(sliceDeck)!=sliceLayer||view[sliceDeck]!=3||p.viewState.oppositeEdit[sliceDeck]){sliceDrag=false;sliceDocument.reset();return;}
        moveSampleNeonSliceMarker(sliceDraft,sliceMarker,slicePosition(sliceDeck,point,sliceViewport));
        if(control(p,sliceDeck,ZeroCross)!=0)snapDeckSliceMarker(sliceDocument->decks[sliceDeck].layers[sliceLayer],sliceDraft,sliceMarker);
        invalid();
    }
    void finishSlice() {
        const bool valid=sliceDocument==p.document&&layer(sliceDeck)==sliceLayer&&view[sliceDeck]==3;
        sliceDrag=false;if(valid)setSliceLayout(sliceDeck,sliceDraft);sliceDocument.reset();
    }
    double jogMouseX=0,jogRemainder=0;bool jogFirst=true;
    void screenPerformance(unsigned d,double x) {
        const auto surface=[&](CRect b,const char* label,bool touch,bool stack=false){
            fill(b,style.cell);stroke(b,style.grid);text(label,b,style.label,kCenterText);
            drag(b,[this,d,stack](CPoint point){if(jogFirst){jogMouseX=point.x;jogFirst=false;return;}const double delta=(point.x-jogMouseX)/2+jogRemainder;
                const int ticks=static_cast<int>(delta);jogRemainder=delta-ticks;jogMouseX=point.x;
                if(ticks)command(p,{stack?CommandKind::StackJog:CommandKind::Jog,d,0,double(ticks)});},
                [this,d,touch]{jogFirst=true;jogRemainder=0;if(touch){guiTouch[d]=true;command(p,{CommandKind::Touch,d,0,1});}},
                [this,d,touch]{if(touch){guiTouch[d]=false;command(p,{CommandKind::Touch,d,0,0});}});
        };
        // No controller protocol is involved: these are the very same platter
        // actions used by a hardware wheel, including inertia and Slip.
        surface(rect(x+16,274,572,54),"SCRATCH / DRAG LEFT OR RIGHT",true);
        surface(rect(x+16,350,572,54),"NUDGE / DRAG LEFT OR RIGHT",false);
        surface(rect(x+16,426,572,28),"STACK / DRAG LEFT OR RIGHT",false,true);
        button(rect(x+16,480,180,20),"RELEASE MOUSE PADS",[this,d]{for(unsigned n=0;n<8;++n)command(p,{CommandKind::ScreenPad,d,n,-1});});
        button(rect(x+208,480,180,20),"STOP BOTH DECKS",[this]{for(unsigned n=0;n<2;++n)command(p,{CommandKind::Stop,n});});
    }
