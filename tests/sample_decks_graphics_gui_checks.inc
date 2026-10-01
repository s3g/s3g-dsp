        if(ok&&portableVstguiRoot&&std::strcmp(pluginId,"org.s3g.s3g-dsp.sample-decks")==0&&std::getenv("S3G_DECKS_GRAPHICS_STRESS")){
            failureStage="Sample Decks large multichannel zoom with concurrent audio";
            const double scale=[document bounds].size.width/nativeWidth;
            const auto redraw=[&]{@autoreleasepool{[document setNeedsDisplay:YES];[document displayIfNeeded];(void)[document dataWithPDFInsideRect:[document bounds]];}};
            const auto click=[&](double x,double y){const auto at=[document convertPoint:NSMakePoint(x*scale,y*scale) toView:nil];
                for(NSEventType type:{NSEventTypeLeftMouseDown,NSEventTypeLeftMouseUp}){
                    NSEvent* e=[NSEvent mouseEventWithType:type location:at modifierFlags:0 timestamp:0 windowNumber:0 context:nil eventNumber:0 clickCount:1 pressure:1];
                    if(type==NSEventTypeLeftMouseDown)[document mouseDown:e];else [document mouseUp:e];}redraw();};
            const auto choice=[&](double x,double y,unsigned n){click(x,y);click(x,y+9.5+20*(n+.5));};
            const auto zoom=[&](unsigned d,double dy){@autoreleasepool{const auto at=[document convertPoint:NSMakePoint((d?1100:300)*scale,330*scale) toView:nil];
                S3GSmokeScrollEvent* e=[[S3GSmokeScrollEvent alloc] initWithLocation:at modifiers:0 deltaX:0 deltaY:dy];[document scrollWheel:e];[e release];redraw();}};
            const unsigned method=std::getenv("S3G_DECKS_GRAPHICS_METHOD")?std::clamp(std::atoi(std::getenv("S3G_DECKS_GRAPHICS_METHOD")),0,8):0;
            for(unsigned d=0;d<2;++d){clap_event_param_value_t value{};value.header={sizeof(value),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};value.param_id=32+512*d+1;value.value=method;
                clap_input_events_t input{&value,[](const clap_input_events_t*)->uint32_t{return 1;},[](const clap_input_events_t* e,uint32_t)->const clap_event_header_t*{return &static_cast<const clap_event_param_value_t*>(e->ctx)->header;}};
                params->flush(plugin,&input,nullptr);}
            std::atomic<bool> done{false},audioOk{true};std::vector<double> audioTimes;audioTimes.reserve(30000);
            const bool active=plugin->activate(plugin,48000,1,256),running=active&&plugin->start_processing(plugin);ok=running&&ok;
            if(running){
                std::thread render([&]{
                    std::array<std::array<float,256>,32> pcm{};std::array<float*,32> channels{};for(unsigned ch=0;ch<32;++ch)channels[ch]=pcm[ch].data();
                    clap_audio_buffer_t bus{};bus.data32=channels.data();bus.channel_count=32;
                    clap_process_t block{};block.frames_count=256;block.audio_outputs=&bus;block.audio_outputs_count=1;
                    std::array<clap_event_midi_t,2> notes{};for(unsigned d=0;d<2;++d){auto& n=notes[d];n.header={sizeof(n),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};n.data[0]=0x90+d;n.data[1]=0x60;n.data[2]=127;}
                    clap_input_events_t input{&notes,[](const clap_input_events_t*)->uint32_t{return 2;},[](const clap_input_events_t* e,uint32_t n)->const clap_event_header_t*{return &(*static_cast<const std::array<clap_event_midi_t,2>*>(e->ctx))[n].header;}};
                    block.in_events=&input;auto next=std::chrono::steady_clock::now();double energy=0;
                    while(!done.load()){
                        const auto start=std::chrono::steady_clock::now();if(plugin->process(plugin,&block)==CLAP_PROCESS_ERROR)audioOk=false;
                        audioTimes.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());block.in_events=nullptr;
                        for(const auto& ch:pcm)for(float v:ch){if(!std::isfinite(v))audioOk=false;energy+=double(v)*v;}
                        next+=std::chrono::microseconds(5333);std::this_thread::sleep_until(next);
                    }
                    if(energy<=0)audioOk=false;
                });
                std::vector<double> paintTimes;
                for(unsigned orientation:{3u,2u,0u,1u}){
                    choice(18+480,139.5,orientation);choice(s3g::decks::DeckLayout::left(1)+480,139.5,orientation);
                    for(unsigned pass=0;pass<48;++pass){const auto start=std::chrono::steady_clock::now();zoom(pass%2,pass%24<12?4:-4);
                        paintTimes.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
                }
                done=true;render.join();plugin->stop_processing(plugin);
                std::sort(paintTimes.begin(),paintTimes.end());std::sort(audioTimes.begin(),audioTimes.end());
                const auto percentile=[](const auto& data,double p){return data[std::min(data.size()-1,size_t(data.size()*p))];};
                std::cout<<"Decks graphics stress method "<<method<<": "<<paintTimes.size()<<" zoom+draw+PDF events (1–64x); median "<<percentile(paintTimes,.5)<<" ms, p99 "<<percentile(paintTimes,.99)<<" ms, max "<<paintTimes.back()<<" ms\n";
                std::cout<<"Concurrent audio: "<<audioTimes.size()<<" blocks; p99 "<<percentile(audioTimes,.99)<<" ms, max "<<audioTimes.back()<<" ms / 5.333 ms deadline; finite audible="<<audioOk.load()<<'\n';
                ok=audioOk.load()&&audioTimes.size()>20&&paintTimes.back()<1000&&ok;
                if(const char* directory=std::getenv("S3G_DECKS_GUI_CAPTURE_DIR")){
                    NSData* pdf=[document dataWithPDFInsideRect:[document bounds]];
                    ok=[pdf writeToFile:[[NSString stringWithUTF8String:directory] stringByAppendingPathComponent:@"sample-decks.large-stack.pdf"] atomically:YES]&&ok;
                }
            }
            if(active)plugin->deactivate(plugin);
        }
