void characterTransitionChecks(){
    for(double rate:{44100.,48000.,96000.})for(unsigned width:{2u,4u,8u,16u}){
        const auto render=[&](unsigned blockSize){
            DeckCharacterFx fx;check(fx.prepare(rate,256),"Character transition preparation");
            std::array<std::array<float,256>,16> pcm{};std::array<float*,16> ptr{};
            for(unsigned ch=0;ch<width;++ch)ptr[ch]=pcm[ch].data();
            std::vector<float> result;unsigned at=0;
            const unsigned boundaries[]{0,1200,2400,2451,2503,4500,6000};
            const unsigned effects[]{0,1,4,3,2,0};const float gains[]{1,0,.1f,.9f,.4f,.75f};
            for(unsigned stage=0;stage<6;++stage){
                auto values=kSampleNeonFxDefaults[effects[stage]];values[11]=gains[stage];
                while(at<boundaries[stage+1]){
                    const unsigned count=std::min(blockSize,boundaries[stage+1]-at);
                    for(unsigned ch=0;ch<width;++ch)std::fill_n(ptr[ch],count,ch%2?-.2f:.2f);
                    // OUT changes expose an abrupt processor reset even when
                    // the internal amount smoother would hide wet onset.
                    fx.process(ptr.data(),width,count,SampleNeonMangleCharacter(effects[stage]),values,0,true,120);
                    for(unsigned n=0;n<count;++n){
                        for(unsigned ch=1;ch<width;++ch)check(std::abs(pcm[ch][n]-(ch%2?-pcm[0][n]:pcm[0][n]))<1.e-6,"FX transition keeps field components coherent");
                        result.push_back(pcm[0][n]);}
                    at+=count;
                }
            }
            float delta=0;for(unsigned n=1;n<result.size();++n)delta=std::max(delta,std::abs(result[n]-result[n-1]));
            check(delta<.004f,"FX punch/switch/release never hard-switches wet or output trim");
            check(std::abs(result[1199]-.2f*std::pow(10.f,12.f/20))<1.e-6,"effect fades fully in");
            check(std::abs(result[2399]-.2f*std::pow(10.f,-36.f/20))<1.e-6,"effect replacement reaches its full target");
            check(std::abs(result.back()-.2f)<1.e-6,"rapid overlapping effects return fully dry");
            return result;
        };
        const auto reference=render(256),split=render(63);
        for(unsigned n=0;n<reference.size();++n)check(std::abs(reference[n]-split[n])<1.e-6,"effect crossfade is independent of host block size");
    }
}

void cueTransitionChecks(){
    for(double rate:{44100.,48000.,96000.}){
        const auto* plugin=create(&factory,&testHost,descriptor.id);check(plugin&&plugin->init(plugin),"cue transition plugin");auto& p=*self(plugin);
        auto doc=std::make_shared<Document>();auto& l=doc->decks[0].layers[0];
        auto source=std::make_shared<SampleAsset>();source->sampleRate=48000;source->channelCount=16;
        for(unsigned ch=0;ch<16;++ch){source->channels[ch].resize(48000);
            for(unsigned n=0;n<48000;++n)source->channels[ch][n]=.3f*(ch%2?-1:1)*(n<24000?1:-1);}
        l.asset=source;l.name="NO CROSSING AT CUES";analyzeLayer(l);
        doc->decks[0].bookmarks[0]={true,0,.27};publish(p,doc,1,false);
        setValue(p,Storage,2);setValue(p,Format,5);setValue(p,Crossfade,0);setValue(p,Output,0);
        setValue(p,param(0,Level),1);setValue(p,param(0,Amount),0);setValue(p,param(0,PadMode),0);
        check(plugin->activate(plugin,rate,1,256)&&plugin->start_processing(plugin),"cue transition activation");
        const auto render=[&](unsigned blockSize){
            reset(plugin);Audio a;a.run(plugin);std::vector<float> result;
            const unsigned boundaries[]{0,2048,4096,6144};unsigned at=0;
            for(unsigned cue=0;cue<3;++cue){
                if(cue<2){setValue(p,param(0,CuePosition),cue?.77:.27);command(p,{CommandKind::Cue,0});}
                else command(p,{CommandKind::Pad,0,0,1});
                while(at<boundaries[cue+1]){const unsigned count=std::min(blockSize,boundaries[cue+1]-at);a.block.frames_count=count;a.run(plugin);
                    for(unsigned n=0;n<count;++n){for(unsigned ch=1;ch<16;++ch)
                        check(std::abs(a.pcm[ch][n]-(ch%2?-a.pcm[0][n]:a.pcm[0][n]))<1.e-6,"cue ramp shares exact timing across all 16 channels");
                        result.push_back(a.pcm[0][n]);}at+=count;}
                check(std::abs(control(p,0,Position)-(cue==1?.77:.27))<1.e-6,"cue safety fade never moves stored timing");
            }
            check(std::abs(result.front())<1.e-7,"cue entry starts from silence even without a zero crossing");
            float maximum=0;for(unsigned n=1;n<result.size();++n)maximum=std::max(maximum,std::abs(result[n]-result[n-1]));
            check(maximum<.02f,"cue/bookmark jumps have bounded outgoing and incoming edges");
            check(std::abs(result[1800])>.1f&&result[3850]<-.1f&&result.back()>.1f,"fade reaches the selected cue audio with both polarities");
            return result;
        };
        const auto reference=render(256),split=render(63);
        for(unsigned n=0;n<reference.size();++n)check(std::abs(reference[n]-split[n])<1.e-5,"cue fade is independent of host block size");
        plugin->stop_processing(plugin);plugin->deactivate(plugin);plugin->destroy(plugin);
    }
}
