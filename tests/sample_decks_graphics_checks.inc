void waveformPeakChecks(){
    SampleAsset a;a.sampleRate=44100;a.channelCount=8;
    for(unsigned ch=0;ch<8;++ch){a.channels[ch].resize(131101);for(unsigned n=0;n<a.frameCount();++n)a.channels[ch][n]=float(int(n%127)-63)/80*(ch%2?-1:1);}
    a.channels[0][65537]=1;a.channels[1][65537]=-1;
    const auto peaks=buildDeckWavePeaks(a);
    check(peaks->bytes()<uint64_t(a.frameCount())*a.channelCount*sizeof(float)/18,"compact peak pyramid below six percent of PCM");
    for(int channel=-1;channel<8;++channel)for(unsigned span:{1u,17u,63u,64u,255u,1024u,8191u,131101u}){
        for(unsigned start:{0u,63u,256u,65520u,131095u}){const unsigned end=std::min(a.frameCount(),start+span);DeckPeak exact;
            for(unsigned ch=channel<0?0:unsigned(channel);ch<(channel<0?8:unsigned(channel+1));++ch)for(unsigned n=start;n<end;++n)exact.add(a.channels[ch][n]);
            unsigned reads=0;const auto display=peaks->query(a,start,end,channel,&reads);
            check(display.low<=exact.low&&display.high>=exact.high,"peak bins never drop transients");
            if(end-start<64)check(display.low==exact.low&&display.high==exact.high,"sample-resolution extrema exact");
            check(reads<=(end-start<64?(end-start)*(channel<0?8:1):17),"zoom query has bounded work");
        }
    }
    const auto combined=peaks->query(a,65536,65540,-1);check(combined.low==-1&&combined.high==1,"opposite polarity channels do not cancel");
    check(peaks->query(a,999999,999999,-1).high==0,"out-of-source query is empty");
    LayerAudio source;source.asset=tone(2);analyzeLayer(source);auto copy=source;
    check(source.waveform==copy.waveform,"undo shares immutable peak data");
    copy.start=.25;copy.end=.75;copy.asset=cropLayer(copy);analyzeLayer(copy);
    check(source.waveform!=copy.waveform&&source.asset->frameCount()==48000&&copy.asset->frameCount()==24000,"crop regenerates peaks without mutating old source");
}
// Optional realistic GUI fixture, kept out of routine short CI runs. Write
// PCM24 in chunks; the state links to these files, so repeated test snapshots
// do not themselves serialize half a gigabyte of embedded PCM.
void graphicsFixture(const char* directory){
    const std::filesystem::path root=directory;check(std::filesystem::is_directory(root),"use a fresh existing fixture directory");
    const auto wav=[&](const std::string& name,unsigned channels,unsigned seconds,unsigned seed){
        const auto path=root/name;check(!std::filesystem::exists(path),"never overwrite an existing fixture");
        std::ofstream file(path,std::ios::binary);const unsigned frames=seconds*44100,bytes=frames*channels*3;
        const auto u32=[&](uint32_t v){file.write(reinterpret_cast<const char*>(&v),4);};
        const auto u16=[&](uint16_t v){file.write(reinterpret_cast<const char*>(&v),2);};
        file.write("RIFF",4);u32(36+bytes);file.write("WAVEfmt ",8);u32(16);u16(1);u16(channels);u32(44100);u32(44100*channels*3);u16(channels*3);u16(24);file.write("data",4);u32(bytes);
        std::vector<char> buffer;buffer.reserve(8192*channels*3);
        for(unsigned at=0;at<frames;){buffer.clear();const unsigned end=std::min(frames,at+8192);
            for(;at<end;++at){const int32_t sample=static_cast<int32_t>(2000000*std::sin((at+seed)*.037)*(0.5+0.5*std::sin(at*.00011)));
                for(unsigned ch=0;ch<channels;++ch){const uint32_t v=static_cast<uint32_t>(ch%2?-sample:sample);buffer.push_back(char(v));buffer.push_back(char(v>>8));buffer.push_back(char(v>>16));}}
            file.write(buffer.data(),buffer.size());}
        check(file.good(),"fixture audio write");return path.string();
    };
    Plugin p;auto doc=std::make_shared<Document>();
    for(unsigned layer=0;layer<17;++layer){auto& l=doc->decks[0].layers[layer];l.name="STEREO "+std::to_string(layer+1);l.path=wav(l.name+".wav",2,5,layer*123);l.asset=tone(2);}
    auto& large=doc->decks[1].layers[0];large.name="FIVE MINUTES / EIGHT CHANNELS";large.path=wav("eight-channel.wav",8,300,53);large.asset=tone(8);
    // Metadata-only check must reject before allocating/decoding a large file.
    std::shared_ptr<const SampleAsset> decoded;std::string error;
    check(!decode(large.path,decoded,error,1024)&&!decoded&&error.find("REMAINING")!=std::string::npos,"oversized Mac import rejected at header preflight");
    auto started=std::chrono::steady_clock::now();check(decode(large.path,decoded,error),"chunked multichannel decode");
    check(decoded->frameCount()==44100*300&&decoded->channelCount==8,"chunked decode retains full duration and all channels");
    for(unsigned n:{0u,65535u,65536u,44100u*300-1})check(std::abs(decoded->channels[0][n]+decoded->channels[7][n])<1.e-7,"chunk edges preserve multichannel phase");
    std::cout<<"Large import decoded "<<double(decoded->frameCount())*32/(1024*1024)<<" MiB in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s\n";
    decoded.reset();p.document=doc;setValue(p,Storage,1);setValue(p,Format,2);
    p.viewState.view={2,0};p.viewState.zoom={1,1};p.viewState.orientation={3,3};
    p.plugin.plugin_data=&p;Bytes state;check(saveState(&p.plugin,&state.out),"linked stress state");
    std::ofstream file(root/"graphics-state.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(state.data.data()),state.data.size());check(file.good(),"fixture state write");
}
