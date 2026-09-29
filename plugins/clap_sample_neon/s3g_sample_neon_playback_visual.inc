    // Playback-only drawing. Editing markers and stack-path interaction remain
    // in the workspace; none of these visualizations alter sample settings.
    static CColor visualColor(uint32_t rgb, uint8_t alpha) {
        auto value=color(rgb); value.alpha=alpha; return value;
    }
    void playbackDot(CRect row, double x, float level, bool reverse) {
        const auto strength=std::sqrt(std::clamp(level,0.f,1.f));
        const double radius=std::clamp(row.getHeight()*.13,1.0,3.0);
        context->saveGlobalState(); context->setClipRect(row);
        ellipse(rect(x-radius-1,row.getCenter().y-radius-1,2*radius+2,2*radius+2),color(0x080808));
        ellipse(rect(x-radius,row.getCenter().y-radius,2*radius,2*radius),
            visualColor(0xf0f0f0,static_cast<uint8_t>(55+200*strength)));
        if (row.getHeight()>=18) {
            const double direction=reverse ? -1 : 1;
            line({x+direction*4,row.getCenter().y-2},{x+direction*7,row.getCenter().y},style.value);
            line({x+direction*7,row.getCenter().y},{x+direction*4,row.getCenter().y+2},style.value);
        }
        context->restoreGlobalState();
    }
    void drawGrainHead(CRect row, const s3g::sample::VoiceCursor& cursor,
        double sourceStart,double sourceRange,double viewStart,double viewRange,bool dot = true) {
        const auto xFor=[&](double source) {
            return row.left+row.getWidth()*((source-sourceStart)/sourceRange-viewStart)/viewRange;
        };
        const double left=xFor(cursor.sourceStartNormalized),right=xFor(cursor.sourceEndNormalized);
        if (right<row.left || left>row.right) return;
        const double amplitude=std::clamp(row.getHeight()*.32,1.0,14.0);
        const float phase=std::clamp(cursor.windowPhase,0.f,1.f);
        context->saveGlobalState(); context->setClipRect(row);
        const auto contour=[&](float begin,float end,CColor shade) {
            if (end<=begin) return;
            auto path=owned(context->createGraphicsPath()); if (!path) return;
            for (int side : {-1,1}) for (unsigned n=0;n<=24;++n) {
                const float t=begin+(end-begin)*static_cast<float>(n)/24.f;
                const CPoint point(xFor(s3g::sample::neonCursorSource(cursor,t)),
                    row.getCenter().y+side*amplitude*s3g::sample::neonCursorEnvelope(cursor,t));
                if (!n) path->beginSubpath(point); else path->addLine(point);
            }
            context->setFrameColor(shade); context->setLineWidth(.75);
            context->drawGraphicsPath(path,CDrawContext::kPathStroked);
        };
        contour(0,phase,visualColor(0x8a8a8a,120));
        contour(phase,1,visualColor(0xe0e0e0,190));
        context->restoreGlobalState();
        const double x=xFor(cursor.sourcePositionNormalized);
        if (dot && x>=row.left && x<=row.right) playbackDot(row,x,cursor.level,cursor.reverse);
    }
    void drawMotionField(CRect wave,double start,double range,double viewStart,double viewRange) {
        const auto f=familySnapshot();
        const double locus=f[NeonFamily::MotionLocus],half=f[NeonFamily::MotionField]*.5;
        const auto xFor=[&](double position) { return wave.left+wave.getWidth()*(start+range*position-viewStart)/viewRange; };
        const double left=xFor(std::max(0.,locus-half)),right=xFor(std::min(1.,locus+half));
        context->saveGlobalState(); context->setClipRect(wave);
        fill(rect(left,wave.top,right-left,wave.getHeight()),visualColor(0xd79a55,15));
        for (const double x : {left,right}) line({x,wave.top},{x,wave.bottom},visualColor(0xb3a084,130),.75);
        const double x=xFor(locus);
        for (double y=wave.top;y<wave.bottom;y+=8) line({x,y},{x,std::min(y+3,wave.bottom)},visualColor(0xb3a084,150));
        context->restoreGlobalState();
    }
    void drawLaneStack(CRect wave,unsigned count,double viewStart,double viewEnd) {
        std::array<float,32> weights {};
        for (unsigned n=0;n<visual.count;++n) {
            const auto& c=visual.cursors[n];
            if (c.layer<count && c.sourceAsset==p.sources[selectedSlot][c.layer].asset.get())
                weights[c.layer]=std::min(1.f,weights[c.layer]+c.level);
        }
        const auto layout=s3g::sample::neonLaneViewLayout(count,wave.getHeight());
        fill(wave,color(0x111414));
        for (unsigned layer=0;layer<layout.rows;++layer) {
            const auto& source=p.sources[selectedSlot][layer];
            const bool selected=layer==p.selectedLayers[selectedSlot].load();
            const double lo=selected ? paramValue(p,slotParamId(selectedSlot,kSlotStart)) : source.start;
            const double hi=selected ? paramValue(p,slotParamId(selectedSlot,kSlotEnd)) : source.end;
            const double range=std::max(1.e-9,hi-lo);
            const auto row=rect(wave.left,wave.top+layout.top(layer),wave.getWidth(),layout.height);
            context->saveGlobalState(); context->setClipRect(row);
            auto shade=visualColor(0xb6bcbc,static_cast<uint8_t>(75+150*std::clamp(weights[layer],0.f,1.f)));
            drawWaveTrace(row,source.asset.get(),shade,true,lo+range*viewStart,lo+range*viewEnd);
            for (unsigned n=0;n<visual.count;++n) {
                const auto& c=visual.cursors[n];
                if (c.layer!=layer || c.sourceAsset!=source.asset.get()) continue;
                const double position=(c.sourcePositionNormalized-lo)/range;
                if (position<viewStart || position>viewEnd) continue;
                const double x=row.left+row.getWidth()*(position-viewStart)/(viewEnd-viewStart);
                line({x,row.top},{x,row.bottom},visualColor(0xd6d6d6,100));
                if (playbackChoice()==2u) drawGrainHead(row,c,lo,range,viewStart,viewEnd-viewStart);
                else playbackDot(row,x,c.level,c.reverse);
            }
            const double labelTop=row.getHeight()<18 ? row.getCenter().y-7 : row.top+1;
            fill(rect(row.left+3,labelTop,116,14),color(0x111414));
            text(format("L%02u / %s",layer+1,source.asset ? format("%.0f%%",weights[layer]*100).c_str() : "EMPTY"),
                rect(row.left+6,labelTop,110,14),style.value,kLeftText,smallFont);
            if (weights[layer]>.01f) fill(rect(row.left+1,row.top+1,2,row.getHeight()-2),style.value);
            context->restoreGlobalState();
        }
    }
