        if(ok&&portableVstguiRoot&&std::strcmp(pluginId,"org.s3g.s3g-dsp.sample-decks")==0&&!std::getenv("S3G_DECKS_GRAPHICS_STRESS")){
            failureStage="Sample Decks cross-deck workspace and scrolling views";
            const double scale=[document bounds].size.width/nativeWidth;
            const auto redraw=[&]{[document setNeedsDisplay:YES];[document displayIfNeeded];(void)[document dataWithPDFInsideRect:[document bounds]];};
            const auto click=[&](double x,double y,unsigned clicks=1){const auto at=[document convertPoint:NSMakePoint(x*scale,y*scale) toView:nil];
                for(NSEventType type:{NSEventTypeLeftMouseDown,NSEventTypeLeftMouseUp}){
                    NSEvent* event=[NSEvent mouseEventWithType:type location:at modifierFlags:0 timestamp:0 windowNumber:0 context:nil eventNumber:0 clickCount:clicks pressure:1];
                    if(type==NSEventTypeLeftMouseDown)[document mouseDown:event];else [document mouseUp:event];}
                params->flush(plugin,nullptr,nullptr);redraw();};
            const auto drawPattern=[&](double x1,double y1,double x2,double y2,NSEventModifierFlags modifiers=0,unsigned clicks=1){
                for(NSEventType type:{NSEventTypeLeftMouseDown,NSEventTypeLeftMouseDragged,NSEventTypeLeftMouseUp}){
                    const bool first=type==NSEventTypeLeftMouseDown;
                    const auto at=[document convertPoint:NSMakePoint((first?x1:x2)*scale,(first?y1:y2)*scale) toView:nil];
                    NSEvent* event=[NSEvent mouseEventWithType:type location:at modifierFlags:modifiers timestamp:0 windowNumber:0 context:nil eventNumber:0 clickCount:clicks pressure:1];
                    if(first)[document mouseDown:event];else if(type==NSEventTypeLeftMouseDragged)[document mouseDragged:event];else [document mouseUp:event];}
                params->flush(plugin,nullptr,nullptr);redraw();};
            const auto choice=[&](double x,double y,unsigned n){click(x,y);click(x,y+9.5+20*(n+.5));};
            const auto read=[&](unsigned index){double value=0;params->get_value(plugin,index+1,&value);return value;};
            const auto require=[&](bool pass,const char* what){if(!pass)std::cerr<<"Decks: "<<what<<'\n';ok=pass&&ok;};
            const auto set=[&](unsigned index,double value){clap_event_param_value_t event{};event.header={sizeof(event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};event.param_id=index+1;event.value=value;
                clap_input_events_t input{&event,[](const clap_input_events_t*)->uint32_t{return 1;},[](const clap_input_events_t* e,uint32_t)->const clap_event_header_t*{return &static_cast<const clap_event_param_value_t*>(e->ctx)->header;}};
                params->flush(plugin,&input,nullptr);};
            const auto capture=[&](const char* name){redraw();if(const char* directory=std::getenv("S3G_DECKS_GUI_CAPTURE_DIR")){
                NSData* pdf=[document dataWithPDFInsideRect:[document bounds]];
                NSString* path=[[NSString stringWithUTF8String:directory] stringByAppendingPathComponent:[NSString stringWithFormat:@"%s.pdf",name]];
                ok=pdf&&[pdf writeToFile:path atomically:YES]&&ok;}};
            const auto headerColor=[&](double x,double y){
                NSData* pdf=[document dataWithPDFInsideRect:NSMakeRect(x*scale,y*scale,3*scale,3*scale)];
                NSImage* image=[[NSImage alloc] initWithData:pdf];
                NSBitmapImageRep* bitmap=[NSBitmapImageRep imageRepWithData:[image TIFFRepresentation]];
                NSColor* color=bitmap?[[bitmap colorAtX:0 y:0] colorUsingColorSpace:[NSColorSpace genericRGBColorSpace]]:nil;
                const std::array<double,3> rgb{{[color redComponent],[color greenComponent],[color blueComponent]}};
                ok=color!=nil&&ok;[image release];return rgb;};
            const auto deckColors=[&](bool leftEdit,bool rightEdit){
                const auto a=headerColor(18+132,52),b=headerColor(818+132,52);
                const auto pa=headerColor(18+132,670),pb=headerColor(818+132,670);
                const auto mixer=headerColor(638+140,52),body=headerColor(18+595,74);
                // The ordinary harness draws at reduced opacity; documentation
                // capture is opaque. Gray-based tints must remain distinguishable.
                const auto cool=[](const auto& c){return c[2]-c[0]>.03&&c[1]>.12&&c[2]<.5;};
                const auto warm=[](const auto& c){return c[0]-c[2]>.03&&c[0]>.15&&c[0]<.5;};
                const bool colors=(leftEdit?warm(a):cool(a))&&(rightEdit?cool(b):warm(b))&&cool(pa)&&warm(pb)
                    &&std::abs(mixer[0]-mixer[2])<.005&&std::abs(body[0]-body[2])<.005;
                if(!colors){std::cerr<<"Deck header colors did not follow editing target / fixed performance deck\n";
                    for(const auto& c:{a,b,pa,pb,mixer,body})std::cerr<<"RGB "<<c[0]<<", "<<c[1]<<", "<<c[2]<<'\n';}
                const auto bottom=headerColor(18+132,64),padBottom=headerColor(818+132,682);
                ok=colors&&(leftEdit?warm(bottom):cool(bottom))&&warm(padBottom)&&ok;};
            const auto wheel=[&](double dx,double dy,NSEventModifierFlags modifiers=0){
                const auto at=[document convertPoint:NSMakePoint(300*scale,330*scale) toView:nil];
                S3GSmokeScrollEvent* e=[[S3GSmokeScrollEvent alloc] initWithLocation:at modifiers:modifiers deltaX:dx deltaY:dy];
                [document scrollWheel:e];redraw();};
            const auto viewTail=[&]{std::vector<uint8_t> bytes;
                clap_ostream_t out{&bytes,[](const clap_ostream_t* s,const void* data,uint64_t n)->int64_t{auto& b=*static_cast<std::vector<uint8_t>*>(s->ctx);const auto* p=static_cast<const uint8_t*>(data);b.insert(b.end(),p,p+n);return n;}};
                const auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
                ok=state&&state->save(plugin,&out)&&bytes.size()>=42&&ok;
                std::array<double,2> result{};if(bytes.size()>=42){std::memcpy(&result[0],bytes.data()+bytes.size()-42+5,8);std::memcpy(&result[1],bytes.data()+bytes.size()-42+13,8);}return result;};
            std::vector<double> parameters;
            for(unsigned n=0;n<params->count(plugin);++n){clap_param_info_t info{};params->get_info(plugin,n,&info);double v=0;params->get_value(plugin,info.id,&v);parameters.push_back(v);}
            redraw();capture("sample-decks.performance");
            deckColors(false,false);
            click(818+466,56); // Right pane: A EDIT; transport remains B below.
            deckColors(false,true);
            click(18+466,56); // Left pane: B EDIT.
            deckColors(true,true);capture("sample-decks.cross-edit");
            for(unsigned n=0;n<parameters.size();++n){clap_param_info_t info{};params->get_info(plugin,n,&info);double v=0;params->get_value(plugin,info.id,&v);ok=v==parameters[n]&&ok;}
            if(!ok)std::cerr<<"Decks view switch changed an audio parameter\n";
            // Rate is the first playback-editor row. A on the RIGHT, B on the LEFT.
            const double beforeB=read(32+512+33);
            click(818+196,111);ok=std::abs(read(32+33)-1)>.01&&read(32+512+33)==beforeB&&ok;
            click(818+196,111,2);ok=std::abs(read(32+33)-1)<1.e-6&&ok;
            const double beforeA=read(32+33);click(18+196,111);ok=read(32+33)==beforeA&&std::abs(read(32+512+33)-1)>.01&&ok;
            click(18+196,111,2);ok=std::abs(read(32+512+33)-1)<1.e-6&&ok;
            click(18+240,56); // Return A waveform, leaving A editor alongside it.
            deckColors(false,true);
            capture("sample-decks.edit-a");
            failureStage="Sample Decks numeric entry fit and selection contrast";
            NSWindow* numberWindow=[[S3GSmokeKeyWindow alloc] initWithContentRect:[parent bounds]
                styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
            [numberWindow setReleasedWhenClosed:NO];[numberWindow setContentView:parent];
            auto* numberEditor=static_cast<NSTextView*>([numberWindow fieldEditor:YES forObject:nil]);
            NSDictionary* originalNumberSelection=[[numberEditor selectedTextAttributes] copy];
            const auto checkNumber=[&](double x,double y,unsigned id,double value,NSString* replacement,const char* name){
                const double previous=read(id);set(id,value);redraw();click(x,y);
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
                auto* field=canvasNumericField(parent);require(field!=nil,"native numeric editor opens");
                if(field){
                    [field selectText:nil];
                    auto* editor=static_cast<NSTextView*>([field currentEditor]);
                    const auto gray=[](NSColor* c,double v){auto* rgb=[c colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
                        return rgb&&std::abs(rgb.redComponent-v)<.005&&std::abs(rgb.greenComponent-v)<.005&&std::abs(rgb.blueComponent-v)<.005;};
                    require(editor&&gray(editor.selectedTextAttributes[NSBackgroundColorAttributeName],69./255.)
                        &&gray(editor.selectedTextAttributes[NSForegroundColorAttributeName],227./255.),"charcoal selection / light text");
                    const NSRect visible=NSIntersectionRect(field.frame,field.superview.bounds);
                    const NSSize textSize=[field.stringValue sizeWithAttributes:@{NSFontAttributeName:field.font}];
                    const bool fits=textSize.width+8*scale<=visible.size.width&&textSize.height<=visible.size.height+.5;
                    if(!fits)std::cerr<<"Decks "<<name<<" numeric text "<<textSize.width<<" x "<<textSize.height
                        <<" does not fit "<<visible.size.width<<" x "<<visible.size.height<<'\n';
                    require(fits,"full-precision numeric text fits without clipping");
                    capture(name);
                    require(setCanvasNumericText(parent,replacement),"numeric edit accepts text");
                    click(x-100,340);
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
                    params->flush(plugin,nullptr,nullptr);redraw();
                    require(std::abs(read(id)-replacement.doubleValue)<2.e-5,"numeric edit commits unchanged precision");
                    require([numberEditor.selectedTextAttributes isEqualToDictionary:originalNumberSelection],"host selection attributes restored");
                }
                set(id,previous);redraw();
            };
            choice(818+120,84.5,0); // A SOURCE / CHOP, on the right.
            checkNumber(818+570,111,32+35,32,@"24","sample-decks.number-slices-a");
            checkNumber(818+570,137,32+25,123.456789,@"234.567891","sample-decks.number-bpm-a");
            click(18+466,56);choice(18+120,84.5,0); // B SOURCE / CHOP, on the left.
            checkNumber(18+570,111,32+512+35,32,@"16","sample-decks.number-slices-b");
            checkNumber(18+570,137,32+512+25,234.567891,@"123.456789","sample-decks.number-bpm-b");
            checkNumber(638+113,85,0,-60,@"-12.125","sample-decks.number-output");
            checkNumber(638+104,621,8,120,@"45.5","sample-decks.number-take-limit");
            [originalNumberSelection release];[parent removeFromSuperview];[numberWindow close];[numberWindow release];
            click(18+240,56);
            choice(818+120,84.5,2); // A STACK PATH page.
            click(818+180,395);ok=read(32+64+static_cast<unsigned>(s3g::sample::NeonFamily::StackShape))==0&&ok;
            capture("sample-decks.stack-path");
            // Neon's Cutups strip replaces the per-step layer menus. All 64
            // steps fit; a layer edit freezes the generated order, not defaults.
            failureStage="Sample Decks Cutups pattern drawing";
            const auto checkCut=[&](bool pass,const char* what){if(!pass)std::cerr<<"Cutups: "<<what<<'\n';ok=pass&&ok;};
            using CutF=s3g::sample::NeonFamily;
            const auto cutId=[](unsigned deck,CutF key,unsigned step=0){return 32+512*deck+64+static_cast<unsigned>(key)+step;};
            set(32,8);set(cutId(0,CutF::CutRegions),64);set(cutId(0,CutF::CutFileOrder),1);set(cutId(0,CutF::CutSourceOrder),1);
            std::array<double,128> otherPattern{};
            for(unsigned n=0;n<64;++n){otherPattern[n]=read(cutId(1,CutF::CutPatternLane,n));otherPattern[64+n]=read(cutId(1,CutF::CutPatternSource,n));}
            choice(818+120,84.5,5);
            const auto stepX=[](unsigned n){return 818+16+(n+.5)*572/64.;};
            drawPattern(stepX(0),186,stepX(0),186);
            checkCut(read(cutId(0,CutF::CutFileOrder))==5&&read(cutId(0,CutF::CutPatternLane))==0,"first layer edit selects Manual / layer 1");
            for(unsigned n=1;n<64;++n)checkCut(read(cutId(0,CutF::CutPatternLane,n))==(std::getenv("S3G_DECKS_GUI_STATE")?2-n%3:0),"generated pattern preserved");
            drawPattern(stepX(1),176+336*.5,stepX(63),511);
            checkCut(read(cutId(0,CutF::CutPatternLane,1))==(std::getenv("S3G_DECKS_GUI_STATE")?1:0)
                &&read(cutId(0,CutF::CutPatternLane,63))==(std::getenv("S3G_DECKS_GUI_STATE")?2:0),"drag across steps including step 64");
            std::array<double,64> lanes{};for(unsigned n=0;n<64;++n)lanes[n]=read(cutId(0,CutF::CutPatternLane,n));
            drawPattern(stepX(32),176+336*.25,stepX(32),176+336*.75,NSEventModifierFlagOption);
            checkCut(read(cutId(0,CutF::CutSourceOrder))==6&&std::abs(read(cutId(0,CutF::CutPatternSource,32))-.75)<1.e-6,"Option-drag changes source position");
            for(unsigned n=0;n<64;++n)checkCut(read(cutId(0,CutF::CutPatternLane,n))==lanes[n]
                &&read(cutId(1,CutF::CutPatternLane,n))==otherPattern[n]&&read(cutId(1,CutF::CutPatternSource,n))==otherPattern[64+n],"source edit preserves layer order / opposite deck");
            drawPattern(stepX(32),400,stepX(32),400,NSEventModifierFlagOption,2);
            checkCut(read(cutId(0,CutF::CutPatternSource,32))==.5,"Option-double-click default");
            capture("sample-decks.cutups-pattern");
            set(cutId(0,CutF::CutRegions),1);redraw();
            drawPattern(1100,344,1500,600,NSEventModifierFlagOption); // Clamp outside the strip to the only step/end.
            checkCut(read(cutId(0,CutF::CutPatternSource))==1,"one-step pattern / outside bounds");
            set(cutId(0,CutF::CutRegions),16);redraw();capture("sample-decks.cutups-pattern-16");
            // The same editor on the LEFT addresses B, never the physical A.
            click(18+466,56);choice(18+120,84.5,5);set(cutId(1,CutF::CutRegions),1);redraw();
            drawPattern(300,260,300,260,NSEventModifierFlagOption);
            checkCut(read(cutId(1,CutF::CutSourceOrder))==6&&std::abs(read(cutId(1,CutF::CutPatternSource))-.25)<1.e-6
                &&read(cutId(0,CutF::CutPatternSource))==1,"left editor targets B");
            click(18+240,56);
            if(!ok)std::cerr<<"Decks Cutups strip: draw/manual snapshot/Option-source/default reset/bounds/target failed\n";
            click(818+240,56); // B waveform.
            deckColors(false,false);
            // If PCM was loaded by the fixture, exercise every real renderer.
            if(std::getenv("S3G_DECKS_GUI_STATE")){
                std::array<std::array<float,256>,32> audio{};std::array<float*,32> channels{};for(unsigned ch=0;ch<32;++ch)channels[ch]=audio[ch].data();
                clap_audio_buffer_t bus{};bus.data32=channels.data();bus.channel_count=32;
                clap_process_t block{};block.audio_outputs=&bus;block.audio_outputs_count=1;block.frames_count=256;
                clap_event_midi_t note{};note.header={sizeof(note),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};note.data[0]=0x90;note.data[1]=0x60;note.data[2]=127;
                clap_input_events_t input{&note,[](const clap_input_events_t*)->uint32_t{return 1;},[](const clap_input_events_t* e,uint32_t)->const clap_event_header_t*{return &static_cast<const clap_event_midi_t*>(e->ctx)->header;}};
                const bool activated=plugin->activate(plugin,48000,1,256);const bool processing=activated&&plugin->start_processing(plugin);ok=processing&&ok;
                if(processing){plugin->process(plugin,&block);
                    // Manual slicing edits the primary view, in either axis,
                    // and one drag is exactly one source-history operation.
                    failureStage="Sample Decks manual slice editing";
                    click(818+466,56);choice(818+120,84.5,0); // A Source / Chop.
                    set(32+1,0);set(32+5,0);set(32+6,1);set(32+34,0);redraw();
                    click(818+100,286);choice(18+480,139.5,2); // Edit markers, horizontal overview.
                    const double sliceCount=read(32+35);
                    drawPattern(34+572*.20,330,34+572*.22,330);
                    require(read(32+35)==sliceCount+1,"horizontal slice add + drag");capture("sample-decks.manual-slices");
                    click(514,22);require(read(32+35)==sliceCount,"one-step slice undo"); // Undo entire add + drag.
                    click(574,22);require(read(32+35)==sliceCount+1,"slice redo");
                    const auto removeMarker=[&](double x,double y){const auto at=[document convertPoint:NSMakePoint(x*scale,y*scale) toView:nil];
                        // AppKit's synthetic factory reports button zero for
                        // right clicks. Control-click is VSTGUI's secondary path.
                        for(NSEventType type:{NSEventTypeLeftMouseDown,NSEventTypeLeftMouseUp}){
                            NSEvent* event=[NSEvent mouseEventWithType:type location:at modifierFlags:NSEventModifierFlagControl timestamp:0 windowNumber:0 context:nil eventNumber:0 clickCount:1 pressure:1];
                            if(type==NSEventTypeLeftMouseDown)[document mouseDown:event];else [document mouseUp:event];}
                        params->flush(plugin,nullptr,nullptr);redraw();};
                    removeMarker(34+572*.22,330);require(read(32+35)==sliceCount,"horizontal slice delete");
                    choice(18+480,139.5,3);drawPattern(300,158+372*.20,300,158+372*.22);
                    require(read(32+35)==sliceCount+1,"vertical slice add + drag");removeMarker(300,158+372*.22);require(read(32+35)==sliceCount,"vertical slice delete");
                    choice(18+100,139.5,0);choice(18+480,139.5,0);click(818+240,56);set(32+34,1);
                    failureStage="Sample Decks hardware pad visual feedback";
                    plugin->reset(plugin);set(32+30,4);set(32+45,1);set(32+31,0);plugin->process(plugin,&block);redraw();
                    const auto idlePad=headerColor(34,730);
                    note.data[0]=0x92;note.data[1]=0x20;note.data[2]=127;block.in_events=&input;plugin->process(plugin,&block);block.in_events=nullptr;redraw();
                    const auto heldPad=headerColor(34,730);
                    require(heldPad[2]>idlePad[2]+.1,"hardware pad highlight");capture("sample-decks.pad-feedback");
                    note.data[0]=0x82;note.data[2]=0;block.in_events=&input;plugin->process(plugin,&block);block.in_events=nullptr;
                    for(unsigned n=0;n<30;++n)plugin->process(plugin,&block);redraw();
                    const auto releasedPad=headerColor(34,730);require(std::abs(releasedPad[2]-idlePad[2])<.03,"hardware pad highlight clears");
                    click(18+554,674);set(32+51,1);redraw();
                    click(18+48,737);click(18+48+72*4,737);plugin->process(plugin,&block);redraw();
                    capture("sample-decks.mouse-chord");
                    const auto latchedPad=headerColor(34,730);require(latchedPad[2]>idlePad[2]+.1,"mouse chord latch highlight");
                    drawPattern(1000,300,1100,300);plugin->process(plugin,&block); // shared scratch action
                    click(818+100,490);plugin->process(plugin,&block); // Release Mouse Pads.
                    set(32+51,0);set(32+45,0);click(818+240,56);
                    failureStage="Sample Decks distinct pad roles";
                    plugin->reset(plugin);set(32+1,0);set(32+31,0);set(32+30,0);redraw();
                    choice(18+374,702.5,1);require(read(32+52)==1,"Cues Store action menu");
                    click(18+48,737);plugin->process(plugin,&block);plugin->on_main_thread(plugin);redraw();
                    require(read(32+52)==0,"cue store returns to Jump");
                    for(unsigned role=0;role<8;++role){set(32+30,role);redraw();capture(("sample-decks.pad-role-"+std::to_string(role)).c_str());}
                    set(32+30,3);redraw();choice(18+374,702.5,1);require(read(32+53)==1,"Slices Held menu");
                    choice(18+374,702.5,0);require(read(32+53)==0,"Slices One Shot menu");
                    set(32+30,2);redraw();choice(18+374,702.5,1);require(read(32+55)==1,"FX Punch Toggle menu");
                    choice(18+374,702.5,0);require(read(32+55)==0,"FX Punch Hold menu");
                    set(32+30,4);redraw();
                    note.data[0]=0x90;note.data[1]=0x60;note.data[2]=127;
                    failureStage="Sample Decks playback views";
                    for(unsigned method=0;method<9;++method){plugin->reset(plugin);set(32,method);set(32+2,4);set(32+1,0);
                        block.in_events=&input;plugin->process(plugin,&block);block.in_events=nullptr;
                        for(unsigned n=0;n<32;++n)plugin->process(plugin,&block);
                        redraw();capture(("sample-decks.method-"+std::to_string(method)).c_str());
                        click(818+466,56);choice(818+120,84.5,1);
                        capture(("sample-decks.edit-method-"+std::to_string(method)).c_str());
                        click(818+240,56);
                    }
                    choice(18+100,139.5,2);capture("sample-decks.stack-lanes");
                    choice(18+480,139.5,1);capture("sample-decks.horizontal");
                    for(unsigned orientation:{2u,3u}){
                        choice(18+480,139.5,orientation);plugin->on_main_thread(plugin);
                        const auto dirtyBeforeZoom=hostContext.stateDirtyCount;
                        wheel(0,4);const auto zoomed=viewTail();
                        wheel(-4,0);const auto panned=viewTail();
                        ok=zoomed[0]>1&&std::abs(zoomed[0]-panned[0])<1.e-9&&panned[1]>zoomed[1]&&ok;
                        wheel(0,4,NSEventModifierFlagShift);const auto back=viewTail();
                        ok=std::abs(back[0]-panned[0])<1.e-9&&back[1]<panned[1]&&ok;
                        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.6]];
                        plugin->on_main_thread(plugin);
                        require(hostContext.stateDirtyCount==dirtyBeforeZoom,"zoom/pan never request a host undo snapshot, even after idle");
                        capture(orientation==2?"sample-decks.horizontal-overview":"sample-decks.vertical-overview");
                    }
                    set(32+30,4);set(32+45,1);redraw();click(18+554,674);
                    ok=read(32+45)==1&&ok;capture("sample-decks.keyboard");
                    failureStage="Sample Decks track-input recording controls";
                    plugin->reset(plugin);plugin->process(plugin,&block);
                    choice(638+80,483.5,1);require(read(10)==1,"Track Input source menu");
                    clap_audio_buffer_t incoming=bus;incoming.channel_count=2;
                    block.audio_inputs=&incoming;block.audio_inputs_count=1;
                    audio[0].fill(.125f);audio[1].fill(-.125f);
                    click(638+80,538);plugin->process(plugin,&block);capture("sample-decks.input-recording");
                    click(638+80,538);plugin->process(plugin,&block);
                    const auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
                    clap_ostream_t sink{nullptr,[](const clap_ostream_t*,const void*,uint64_t n)->int64_t{return static_cast<int64_t>(n);}};
                    bool saved=false;NSDate* deadline=[NSDate dateWithTimeIntervalSinceNow:2];
                    do{plugin->on_main_thread(plugin);saved=state->save(plugin,&sink);if(!saved)[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];}while(!saved&&[deadline timeIntervalSinceNow]>0);
                    require(saved,"record toggle stops and commits input take");capture("sample-decks.input-ready");
                    choice(638+80,483.5,2);require(read(10)==2,"Input + Thru menu");
                    choice(638+80,483.5,0);require(read(10)==0,"Deck Output remains available");
                    block.audio_inputs=nullptr;block.audio_inputs_count=0;
                    plugin->stop_processing(plugin);}
                if(activated)plugin->deactivate(plugin);
            }
            if(!ok)std::cerr<<"Decks cross-deck controls/default reset/path interaction failed\n";
        }
