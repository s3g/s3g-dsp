    // The same voice/envelope data and contour functions as Neon, projected
    // onto either a vertical or horizontal time axis. No GUI-generated voices.
    struct Projection {
        CRect bounds;bool vertical;
        CPoint at(double time,double amplitude=0) const {return vertical
            ?CPoint(bounds.getCenter().x+amplitude,bounds.top+time*bounds.getHeight())
            :CPoint(bounds.left+time*bounds.getWidth(),bounds.getCenter().y-amplitude);}
        double across() const {return vertical?bounds.getWidth():bounds.getHeight();}
        double length() const {return vertical?bounds.getHeight():bounds.getWidth();}
    };
    static CColor visualColor(uint32_t rgb,unsigned alpha){auto c=color(rgb);c.alpha=static_cast<uint8_t>(std::min(255u,alpha));return c;}
    float layerWeight(unsigned d,unsigned layerIndex) const {
        float sum=0;const auto* asset=p.document->decks[d].layers[layerIndex].asset.get();
        for(unsigned n=0;n<visual[d].count;++n){const auto& c=visual[d].cursors[n];if(c.layer==layerIndex&&c.sourceAsset==asset)sum+=c.level;}
        return std::clamp(sum,0.f,1.f);
    }
    double sourceStart(unsigned d,unsigned n) const {return n==layer(d)?control(p,d,Start):p.document->decks[d].layers[n].start;}
    double sourceRange(unsigned d,unsigned n) const {return std::max(1.e-9,(n==layer(d)?control(p,d,End):p.document->decks[d].layers[n].end)-sourceStart(d,n));}
    double followPosition(unsigned d) const {
        if(!p.playing[d].load())return -1;
        const unsigned method=static_cast<unsigned>(control(p,d,Playback));
        if(method==1||method==4||(method==2&&control(p,d,family(F::GrainSource))!=0))return p.scanPosition[d].load();
        const unsigned n=std::min(31u,p.soundingLayer[d].load());
        return view[d]==1||view[d]==3?p.cursor[d].load():(p.cursor[d].load()-sourceStart(d,n))/sourceRange(d,n);
    }
    DeckViewport viewport(unsigned d) const {
        return DeckViewport::make(zoom[d],offset[d],followPosition(d),p.viewState.scrolling(d)&&view[d]!=1&&view[d]!=3);
    }
    void timeLine(const Projection& r,double fraction,CColor shade,double width=1){
        if(fraction<0||fraction>1)return;line(r.at(fraction,-r.across()*.5),r.at(fraction,r.across()*.5),shade,width);
    }
    struct WaveTrace {
        std::weak_ptr<const DeckWavePeaks> peaks;
        CRect bounds;bool vertical=false;int channel=0;
        double lo=0,range=0,first=0,last=0;
        SharedPointer<CGraphicsPath> path;
    };
    std::array<std::array<WaveTrace,64>,2> waveTraces;
    void trace(unsigned d,unsigned index,unsigned cacheIndex,const Projection& r,const DeckViewport& v,int channel,bool window,unsigned alpha){
        const auto& l=p.document->decks[d].layers[index];if(!l.asset||!l.waveform||!alpha)return;
        const auto& a=*l.asset;const double lo=window?sourceStart(d,index):0,range=window?sourceRange(d,index):1;
        auto& cache=waveTraces[d][cacheIndex];
        if(!cache.path||cache.peaks.lock()!=l.waveform||cache.bounds!=r.bounds||cache.vertical!=r.vertical||cache.channel!=channel
            ||cache.lo!=lo||cache.range!=range||cache.first!=v.at(0)||cache.last!=v.at(1)){
            cache.path=owned(context->createGraphicsPath());if(!cache.path)return;
            cache.peaks=l.waveform;cache.bounds=r.bounds;cache.vertical=r.vertical;cache.channel=channel;
            cache.lo=lo;cache.range=range;cache.first=v.at(0);cache.last=v.at(1);
            const unsigned pixels=std::max(1u,static_cast<unsigned>(r.length()));
            for(unsigned pixel=0;pixel<pixels;++pixel){const double t=v.at(double(pixel)/pixels),next=v.at(double(pixel+1)/pixels);if(next<=0||t>=1)continue;
                const auto begin=static_cast<unsigned>(std::clamp(lo+range*std::max(0.,t),0.,1.)*(a.frameCount()-1));
                const auto end=std::min(a.frameCount(),static_cast<unsigned>(std::clamp(lo+range*std::min(1.,next),0.,1.)*(a.frameCount()-1))+1);
                const auto peak=l.waveform->query(a,begin,end,channel);
                cache.path->beginSubpath(r.at(double(pixel)/pixels,std::clamp(peak.low,-1.f,1.f)*r.across()*.43));
                cache.path->addLine(r.at(double(pixel)/pixels,std::clamp(peak.high,-1.f,1.f)*r.across()*.43));
            }
        }
        context->setFrameColor(visualColor(0xa5aaaa,alpha));context->setLineWidth(1);
        context->drawGraphicsPath(cache.path,CDrawContext::kPathStroked);
    }
    void voiceHead(unsigned d,unsigned index,const Projection& r,const DeckViewport& v,bool window,const VoiceCursor& head,bool lane){
        const double lo=window?sourceStart(d,index):0,range=window?sourceRange(d,index):1;
        const auto tFor=[&](double absolute){return v.fraction((absolute-lo)/range);};
        const unsigned method=static_cast<unsigned>(control(p,d,Playback));
        const bool envelope=method==2||method==3||method==8;
        if(envelope&&r.across()>=24){const double amplitude=std::clamp(r.across()*.32,1.,14.);
            const auto contour=[&](float begin,float end,CColor shade){
                if(end<=begin)return;for(int side:{-1,1}){auto path=owned(context->createGraphicsPath());if(!path)continue;
                    for(unsigned n=0;n<=24;++n){const float phase=begin+(end-begin)*n/24.f;
                        const auto point=r.at(tFor(neonCursorSource(head,phase)),side*amplitude*neonCursorEnvelope(head,phase));
                        if(!n)path->beginSubpath(point);else path->addLine(point);}
                    context->setFrameColor(shade);context->setLineWidth(.75);context->drawGraphicsPath(path,CDrawContext::kPathStroked);}};
            const float phase=std::clamp(head.windowPhase,0.f,1.f);
            contour(0,phase,visualColor(0x8a8a8a,120));contour(phase,1,visualColor(0xe0e0e0,190));
        }else if(method==5||method==7){
            timeLine(r,tFor(head.sourceStartNormalized),visualColor(0x9aa8a8,70));
            timeLine(r,tFor(head.sourceEndNormalized),visualColor(0x9aa8a8,70));
        }
        const double t=tFor(head.sourcePositionNormalized);if(t<0||t>1)return;
        timeLine(r,t,visualColor(0xd6d6d6,lane?100:180));
        if(lane||envelope){const auto point=r.at(t);const double radius=std::clamp(r.across()*.13,1.,3.);
            ellipse(rect(point.x-radius-1,point.y-radius-1,2*radius+2,2*radius+2),color(0x080808));
            ellipse(rect(point.x-radius,point.y-radius,2*radius,2*radius),visualColor(0xe0e0e0,55+static_cast<unsigned>(200*std::sqrt(std::clamp(head.level,0.f,1.f)))));
            if(r.across()>=18){const double direction=head.reverse?-1:1;const auto tip=r.at(t+direction*7/r.length());
                line(r.at(t+direction*4/r.length(),-2),tip,style.value);line(tip,r.at(t+direction*4/r.length(),2),style.value);}
        }
    }
    void motionField(unsigned d,unsigned index,const Projection& r,const DeckViewport& v,bool window){
        const auto f=pathSettings(d);const double locus=f[F::MotionLocus],half=f[F::MotionField]*.5;
        const double lo=window?0:sourceStart(d,index),range=window?1:sourceRange(d,index);
        const double first=v.fraction(lo+range*std::max(0.,locus-half)),last=v.fraction(lo+range*std::min(1.,locus+half));
        const auto a=r.at(first,-r.across()*.5),b=r.at(last,r.across()*.5);
        fill(CRect(std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)),visualColor(0xd79a55,15));
        timeLine(r,first,visualColor(0xb3a084,130),.75);timeLine(r,last,visualColor(0xb3a084,130),.75);
        const double t=v.fraction(lo+range*locus);
        if(t>=0&&t<=1)for(double n=-r.across()*.5;n<r.across()*.5;n+=8)line(r.at(t,n),r.at(t,std::min(n+3,r.across()*.5)),visualColor(0xb3a084,150));
    }
    void waveform(unsigned d){const auto b=wave(d);const bool vertical=p.viewState.vertical(d),lanes=view[d]==2,window=view[d]!=1&&view[d]!=3;
        const auto v=viewport(d);const auto& doc=p.document->decks[d];const int playing=p.waveformLayer[d].load();
        auto blend=neonStackBlend(p.stackPosition[d].load(),&doc.playbackStack(layer(d)));
        if(!window||playing==-1)blend={layer(d),layer(d),0};else if(playing>=0)blend={static_cast<unsigned>(playing),static_cast<unsigned>(playing),0};
        if(blend.first>=32||blend.second>=32)blend={layer(d),layer(d),0};
        const auto& shown=doc.layers[blend.first];
        const unsigned rows=lanes?std::max(1u,unsigned(doc.stack.count)):p.viewState.channels[d]==0&&shown.asset?shown.asset->channelCount:1;
        fill(b,color(0x111414));stroke(b,style.grid);
        for(unsigned row=0;row<rows;++row){const unsigned index=lanes?row:blend.first;
            const auto bounds=vertical?rect(b.left+row*b.getWidth()/rows,b.top,b.getWidth()/rows,b.getHeight()):rect(b.left,b.top+row*b.getHeight()/rows,b.getWidth(),b.getHeight()/rows);
            const Projection r{bounds,vertical};context->saveGlobalState();context->setClipRect(bounds);
            line(r.at(0),r.at(1),style.grid);
            const int channel=lanes||p.viewState.channels[d]==1?-1:p.viewState.channels[d]==2?0:row;
            const unsigned alpha=lanes?75+static_cast<unsigned>(150*layerWeight(d,row)):blend.first==blend.second?200:static_cast<unsigned>(200*(1-blend.mix));
            trace(d,index,row*2,r,v,channel,window,alpha);
            if(!lanes&&blend.second!=blend.first)trace(d,blend.second,row*2+1,r,v,channel,window,static_cast<unsigned>(200*blend.mix));
            if(control(p,d,Playback)==1)motionField(d,index,r,v,window);
            const auto& source=doc.layers[index];
            const auto& slices=sliceDrag&&sliceDeck==d&&sliceLayer==index&&sliceDocument==p.document?sliceDraft:source.slices;
            if(source.asset){for(unsigned n=1;n<slices.sliceCount;++n){const double slice=window?slices.boundaries[n]:sourceStart(d,index)+sourceRange(d,index)*slices.boundaries[n];timeLine(r,v.fraction(slice),visualColor(0xa0beb5,view[d]==3?220:80),view[d]==3?2:1);}
                if(!window){timeLine(r,v.fraction(control(p,d,Start)),style.value,2);timeLine(r,v.fraction(control(p,d,End)),style.value,2);}}
            for(unsigned n=0;n<visual[d].count;++n){const auto& head=visual[d].cursors[n];const bool match=head.layer==index||(!lanes&&head.layer==blend.second);
                if(!match||head.layer>=32||head.sourceAsset!=doc.layers[head.layer].asset.get())continue;
                voiceHead(d,head.layer,r,v,window,head,lanes);}
            const double moving=followPosition(d);if(p.viewState.scrolling(d)&&window&&moving>=0)timeLine(r,v.fraction(moving),visualColor(d?0xc99a78:0x91b8bf,170));
            context->restoreGlobalState();
            const std::string label=lanes?format("%02u",row+1):channel<0?"PEAKS":value(p,Format)>=3?format("ACN %u",channel):format("CH %u",channel+1);
            const auto caption=vertical?rect(bounds.left+1,bounds.top+2,bounds.getWidth()-2,13):rect(bounds.left+3,bounds.top,lanes?30:56,std::min(14.,bounds.getHeight()));
            text(label,caption,style.value,vertical?kCenterText:kLeftText,smallFont);
            if(lanes&&layerWeight(d,row)>.01f){if(vertical)fill(rect(bounds.left,bounds.bottom-3,bounds.getWidth(),2),style.value);else fill(rect(bounds.left,bounds.top,2,bounds.getHeight()),style.value);}
        }
        if(!doc.base())text("DROP AUDIO / ADD AUDIO",b,style.dim,kCenterText);
        const double duration=shown.asset?shown.asset->frameCount()/shown.asset->sampleRate*(window?sourceRange(d,blend.first):1):0;
        for(unsigned tick=1;tick<5;++tick){const double fraction=tick/5.,seconds=v.at(fraction)*duration;if(seconds<0||seconds>duration)continue;
            const auto label=vertical?rect(b.right-58,b.top+fraction*b.getHeight(),54,12):rect(b.left+fraction*b.getWidth(),b.bottom-13,54,12);
            text(format(duration*v.width<.1?"%.3f S":"%.2f S",seconds),label,style.dim,kRightText,smallFont);}
        text(format("%s / L%02u%s / %.1fX",kMethods[static_cast<unsigned>(control(p,d,Playback))],blend.first+1,blend.first!=blend.second?format(" > %02u / %.0f%%",blend.second+1,blend.mix*100).c_str():"",zoom[d]),rect(b.left,532,b.getWidth(),12),style.value,kLeftText,smallFont);
        if(view[d]==3)return;
        // Lock the viewport for the complete mouse gesture; a moving waveform
        // must not move the pointer's target underneath a drag.
        drag(b,[this,d,b,vertical,window,index=blend.first](CPoint point){const double fraction=vertical?(point.y-b.top)/b.getHeight():(point.x-b.left)/b.getWidth();
            const double normalized=std::clamp(dragViewport.at(fraction),0.,1.);
            const double position=window?normalized:(normalized-sourceStart(d,index))/sourceRange(d,index);
            command(p,{CommandKind::Seek,d,0,std::clamp(position,0.,1.)});},
            [this,d,v]{dragViewport=v;guiTouch[d]=true;command(p,{CommandKind::Touch,d,0,1});},[this,d]{guiTouch[d]=false;command(p,{CommandKind::Touch,d,0,0});});
    }
    void stackPath(unsigned d,CRect r){const auto f=pathSettings(d);fill(r,style.cell);stroke(r,style.grid);
        context->saveGlobalState();context->setClipRect(r);
        const unsigned count=std::max(1u,unsigned(p.document->decks[d].stack.count));
        for(unsigned n=0;n<count;++n){const double y=r.top+(count>1?double(n)/(count-1):0)*r.getHeight();line({r.left,y},{r.right,y},visualColor(0x484848,100));}
        if(auto path=owned(context->createGraphicsPath())){
            path->beginSubpath({r.left,r.top+neonStackPath(f,0)*r.getHeight()});
            for(unsigned i=1;i<=128;++i){const double t=i/128.;path->addLine({r.left+t*r.getWidth(),r.top+neonStackPath(f,t)*r.getHeight()});}
            context->setFrameColor(style.value);context->setLineWidth(1);context->drawGraphicsPath(path,CDrawContext::kPathStroked);
        }
        for(unsigned n=0;n<unsigned(f[F::PathCount]);++n){const auto x=r.left+f.values[neonFamilyIndex(F::PathTime)+n]*r.getWidth(),y=r.top+f.values[neonFamilyIndex(F::PathValue)+n]*r.getHeight();fill(rect(x-2,y-2,4,4),style.text);}
        const bool manual=control(p,d,StackNavigation)!=0;const float phase=p.pathPhase[d].load();
        if(manual){const double y=r.top+p.stackPosition[d].load()*r.getHeight();line({r.left,y},{r.right,y},color(0x91b8bf));}
        else if(phase>=0){const double x=r.left+phase*r.getWidth(),y=r.top+neonStackPath(f,phase)*r.getHeight();line({x,r.top},{x,r.bottom},visualColor(0xaaaaaa,80));ellipse(rect(x-3,y-3,6,6),style.text);}
        text(manual?"MANUAL STACK":"L01",rect(r.left+4,r.top+2,110,12),style.value,kLeftText,smallFont);
        text(format("L%02u",count),rect(r.left+4,r.bottom-14,40,12),style.value,kLeftText,smallFont);
        context->restoreGlobalState();
    }
