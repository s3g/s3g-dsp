// Versioned, explicit scalar serialization. No pointers, C++ object layouts or
// transient recording/playback state enter project files. Parse before commit.
template<class T> bool writeScalar(const clap_ostream_t* s,const T& v){return s3g::clap_state::writeAll(s,&v,sizeof(v));}
template<class T> bool readScalar(const clap_istream_t* s,T& v){return s3g::clap_state::readAll(s,&v,sizeof(v));}
bool writeString(const clap_ostream_t* s,const std::string& v){const auto n=static_cast<uint32_t>(v.size());return n<=32768&&writeScalar(s,n)&&s3g::clap_state::writeAll(s,v.data(),n);}
bool readString(const clap_istream_t* s,std::string& v){uint32_t n=0;if(!readScalar(s,n)||n>32768)return false;v.resize(n);return s3g::clap_state::readAll(s,v.data(),n)&&v.find('\0')==std::string::npos;}
bool saveState(const clap_plugin_t* plugin,const clap_ostream_t* stream){
    auto& p=*self(plugin);if(p.busy||p.capture.state.load())return false;
    try {
        if(!writeScalar(stream,uint32_t(0x334b4453))||!writeScalar(stream,uint32_t(7))||!writeScalar(stream,uint32_t(kParamCount)))return false;
        for(unsigned i=0;i<kParamCount;++i)if(!writeScalar(stream,value(p,i)))return false;
        media::ProjectLocation project;const auto context=media::reaperContext(p.host);
        const bool projectReady=media::queryProjectLocation(context,project);
        const auto mode=static_cast<unsigned>(value(p,Storage));
        for(unsigned d=0;d<2;++d)for(unsigned n=0;n<32;++n){const auto& l=p.document->decks[d].layers[n];
            // A crop/normalize clears its locator. A registration may still
            // refer to the previous layer and must never replace edited PCM.
            std::string path=l.path;
            uint8_t type=l.asset?1:0; // 1 embedded, 2 absolute, 3 project-relative
            if(l.asset&&mode!=2) {
                if(mode==0&&projectReady){
                    if(const auto* cached=preparedMedia(p,d,n,project)){
                        // Honor a host-managed media rename without rehashing.
                        const auto& registered=p.registrations[d][n].absolutePath();
                        const bool current=cached==&p.projectMedia[d][n];
                        const auto& absolute=!current||registered.empty()?cached->copy.absolutePath:registered;
                        if(media::makeProjectRelativePath(project,absolute,path,nullptr))type=3;
                    }
                    // Pending/failed collections embed the loaded PCM, never
                    // save an unverified or not-yet-existing relative locator.
                }
                else if(mode==1&&!path.empty())type=2;
            }
            if(!writeScalar(stream,type))return false;if(!type)continue;
            if(!writeString(stream,l.name)||!writeScalar(stream,l.start)||!writeScalar(stream,l.end)||!writeScalar(stream,l.slices.sliceCount))return false;
            for(double v:l.slices.boundaries)if(!writeScalar(stream,v))return false;
            if(type>=2){if(!writeString(stream,path))return false;continue;}
            const auto& a=*l.asset;if(!writeScalar(stream,a.sampleRate)||!writeScalar(stream,a.channelCount)||!writeScalar(stream,a.frameCount()))return false;
            for(unsigned ch=0;ch<a.channelCount;++ch)if(!s3g::clap_state::writeAll(stream,a.channels[ch].data(),a.frameCount()*sizeof(float)))return false;
        }
        for(const auto& deck:p.document->decks)for(const auto& cue:deck.bookmarks)
            if(!writeScalar(stream,uint8_t(cue.set))||!writeScalar(stream,cue.layer)||!writeScalar(stream,cue.position))return false;
        const auto& v=p.viewState;
        for(unsigned d=0;d<2;++d)if(!writeScalar(stream,v.oppositeEdit[d])||!writeScalar(stream,v.page[d])||!writeScalar(stream,v.view[d])
            ||!writeScalar(stream,v.channels[d])||!writeScalar(stream,v.orientation[d])||!writeScalar(stream,v.zoom[d])||!writeScalar(stream,v.offset[d]))return false;
        return true;
    }catch(...){return false;}
}
bool loadState(const clap_plugin_t* plugin,const clap_istream_t* stream){
    auto& p=*self(plugin);if(p.capture.state.load()||p.busy)return false;
    try {
        uint32_t magic=0,version=0,total=0;
        if(!readScalar(stream,magic)||magic!=0x334b4453||!readScalar(stream,version)||version<1||version>7||!readScalar(stream,total)||total!=kParamCount)return false;
        std::array<float,kParamCount> values{};
        for(unsigned i=0;i<total;++i){if(!readScalar(stream,values[i])||!std::isfinite(values[i]))return false;const auto& def=p.defs[i];
            if(version<5&&i>=RecordSource&&i<GlobalCount)values[i]=static_cast<float>(def.initial);
            if(version<3&&i>=kDeckBase&&(i-kDeckBase)%kDeckStride>=SamplerPads&&(i-kDeckBase)%kDeckStride<ControlCount)values[i]=static_cast<float>(def.initial);
            if(version<4&&i>=kDeckBase&&(i-kDeckBase)%kDeckStride>=ScreenVelocity&&(i-kDeckBase)%kDeckStride<ControlCount)values[i]=static_cast<float>(def.initial);
            if(version<6&&i>=kDeckBase&&(i-kDeckBase)%kDeckStride>=CueAction&&(i-kDeckBase)%kDeckStride<ControlCount)values[i]=static_cast<float>(def.initial);
            if(version<7&&i>=kDeckBase&&(i-kDeckBase)%kDeckStride==CueLayer)values[i]=static_cast<float>(def.initial);
            if(!def.name.empty()&&(values[i]<def.lo||values[i]>def.hi||(def.integer&&values[i]!=std::round(values[i]))))return false;}
        for(unsigned d=0;d<2;++d){NeonFamilySettings f;for(unsigned i=0;i<kNeonFamilyCount;++i)f.values[i]=values[param(d,kFamily+i)];if(!f.valid())return false;
            if(values[param(d,End)]<=values[param(d,Start)])return false;}
        auto doc=std::make_shared<Document>();uint64_t bytes=0;
        for(unsigned d=0;d<2;++d){for(auto& l:doc->decks[d].layers){uint8_t type=0;
            if(!readScalar(stream,type)||type>3)return false;if(!type)continue;
            if(!readString(stream,l.name)||!readScalar(stream,l.start)||!readScalar(stream,l.end)||!std::isfinite(l.start)||!std::isfinite(l.end)||l.start<0||l.end>1||l.end<=l.start||!readScalar(stream,l.slices.sliceCount))return false;
            for(double& v:l.slices.boundaries)if(!readScalar(stream,v))return false;if(!l.slices.valid())return false;
            if(type>=2){std::string path,error;if(!readString(stream,path))return false;
                if(type==3){if(!media::resolveProjectRelativePath(media::reaperContext(p.host),path,l.path))return false;}else l.path=path;
                if(!decode(l.path,l.asset,error,kAudioBudget-bytes))return false;
            }else{auto a=std::make_shared<SampleAsset>();uint32_t frames=0;
                if(!readScalar(stream,a->sampleRate)||!std::isfinite(a->sampleRate)||a->sampleRate<8000||a->sampleRate>384000||!readScalar(stream,a->channelCount)||!sampleNeonChannelCountSupported(a->channelCount)||!readScalar(stream,frames)||!frames||uint64_t(frames)*a->channelCount*sizeof(float)>kAudioBudget-bytes)return false;
                for(unsigned ch=0;ch<a->channelCount;++ch){a->channels[ch].resize(frames);if(!s3g::clap_state::readAll(stream,a->channels[ch].data(),frames*sizeof(float)))return false;}
                if(!a->valid())return false;l.asset=std::move(a);
            }
            bytes+=uint64_t(l.asset->frameCount())*l.asset->channelCount*sizeof(float);if(bytes>kAudioBudget)return false;
            analyzeLayer(l);
        }}
        if(version>=6)for(auto& deck:doc->decks)for(auto& cue:deck.bookmarks){uint8_t set=0;
            if(!readScalar(stream,set)||set>1||!readScalar(stream,cue.layer)||cue.layer>=32||!readScalar(stream,cue.position)||!std::isfinite(cue.position)||cue.position<0||cue.position>1)return false;
            cue.set=set!=0;if(cue.set&&!deck.layers[cue.layer].asset)return false;}
        DeckViewState view;
        if(version>=2)for(unsigned d=0;d<2;++d)if(!readScalar(stream,view.oppositeEdit[d])||!readScalar(stream,view.page[d])||!readScalar(stream,view.view[d])
            ||!readScalar(stream,view.channels[d])||!readScalar(stream,view.orientation[d])||!readScalar(stream,view.zoom[d])||!readScalar(stream,view.offset[d]))return false;
        if(!view.valid())return false;
        p.viewState=view;
        doc->action="LOAD SET";for(unsigned i=0;i<total;++i)p.values[i].store(values[i]);
        p.undo.clear();p.redo.clear();p.lastTake[0].store(-1);p.lastTake[1].store(-1);p.nextTake=-1;for(auto& d:p.registrations)for(auto& registration:d)registration.clear();
        publish(p,std::move(doc),3,false);p.forceStop.store(3);p.message="SET LOADED; DECKS STOPPED";return true;
    }catch(...){return false;}
}
const clap_plugin_state_t stateExtension{saveState,loadState};
