// The real failure was REAPER's undo snapshot, not a slow waveform paint.
// Model the owning-project bridge and test state saves in Project mode.
struct DeckProjectProbe {
    std::string file,media;
    unsigned registrations=0,unregistrations=0;
};
DeckProjectProbe* deckProjectProbe=nullptr;
void* decksProjectContext(const clap_host_t* host,int selector){return selector==3?host->host_data:selector==4?reinterpret_cast<void*>(uintptr_t(1)):nullptr;}
void* decksEnumProject(int index,char* path,int capacity){if(index||!deckProjectProbe)return nullptr;
    std::snprintf(path,capacity,"%s",deckProjectProbe->file.c_str());return deckProjectProbe;}
void decksProjectPath(void*,char* path,int capacity){std::snprintf(path,capacity,"%s",deckProjectProbe->media.c_str());}
int decksRegisterMedia(const char* name,void*){if(std::strcmp(name,"file_in_project_ex2")==0)++deckProjectProbe->registrations;
    if(std::strcmp(name,"-file_in_project_ex2")==0)++deckProjectProbe->unregistrations;return 1;}
void* decksProjectFunction(const char* name){
    if(!std::strcmp(name,"clap_get_reaper_context"))return reinterpret_cast<void*>(&decksProjectContext);
    if(!std::strcmp(name,"EnumProjects"))return reinterpret_cast<void*>(&decksEnumProject);
    if(!std::strcmp(name,"GetProjectPathEx"))return reinterpret_cast<void*>(&decksProjectPath);return nullptr;
}
void projectMediaSnapshotChecks(){
    char temporary[]="/private/tmp/s3g-decks-project-save.XXXXXX";check(mkdtemp(temporary)!=nullptr,"project snapshot temp directory");
    const std::filesystem::path root=temporary;DeckProjectProbe probe{(root/"session.rpp").string(),(root/"Media").string()};deckProjectProbe=&probe;
    media::ReaperHostBridge bridge{1,nullptr,decksRegisterMedia,decksProjectFunction};
    auto host=testHost;host.host_data=&probe;
    static media::ReaperHostBridge* activeBridge=nullptr;activeBridge=&bridge;
    host.get_extension=[](const clap_host_t*,const char* id)->const void*{return !std::strcmp(id,"cockos.reaper_extension")?activeBridge:nullptr;};
    const auto* plugin=create(&factory,&host,descriptor.id);check(plugin&&plugin->init(plugin),"project snapshot plugin");auto& p=*self(plugin);
    auto doc=std::make_shared<Document>();const auto asset=tone(2);std::array<const float*,16> channels{};channels[0]=asset->channels[0].data();channels[1]=asset->channels[1].data();
    // Separate imported assets, not clipboard aliases: the pending-state test
    // must still exercise seventeen files' PCM after shared-audio deduplication.
    for(unsigned n=0;n<17;++n){auto& l=doc->decks[0].layers[n];l.asset=std::make_shared<SampleAsset>(*asset);l.name="LAYER "+std::to_string(n+1);l.path=(root/(l.name+".wav")).string();std::string error;
        check(s3g::audio_file::writePlanarFloatWaveAtomically(l.path,asset->sampleRate,2,asset->frameCount(),channels.data(),error),"project fixture WAV");analyzeLayer(l);}
    publish(p,doc,1,false);setValue(p,Storage,0);setValue(p,param(0,Playback),1);
    const auto revision=p.revision;const auto historyCount=p.undo.size();
    Bytes pending;check(saveState(plugin,&pending.out),"save before collection remains safe");
    check(!std::filesystem::exists(root/"Media"/"s3g Samples")&&!p.mediaWorker.valid(),"state save never hashes, copies, starts workers or collects project media");
    check(pending.data.size()>6000000,"pending project media embeds actual PCM");
    const auto collect=[&]{p.mediaQueryAfter={};serviceProjectMedia(p);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(p.mediaWorker.valid()&&std::chrono::steady_clock::now()<deadline){serviceProjectMedia(p);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(!p.mediaWorker.valid(),"background project collection completes");};
    collect();for(unsigned n=0;n<17;++n)check(p.projectMedia[0][n].copy.success,"every project layer is prepared");
    check(p.revision==revision&&p.undo.size()==historyCount,"media collection never resets DSP or edits source history");
    Bytes ready;check(saveState(plugin,&ready.out)&&ready.data.size()<30000,"prepared project snapshot contains only locators");
    // Source no longer available: saves must reuse the verified project copy,
    // not reread/hash the original or silently fall back to large embedded PCM.
    for(unsigned n=0;n<17;++n)std::filesystem::rename(doc->decks[0].layers[n].path,doc->decks[0].layers[n].path+".away");
    check(plugin->activate(plugin,48000,1,256)&&plugin->start_processing(plugin),"Motion snapshot playback");Audio audio;
    command(p,{CommandKind::Play,0});check(audio.run(plugin,4)>0,"Motion audible before snapshots");
    const auto begin=std::chrono::steady_clock::now();const auto registrationCount=probe.registrations;
    for(unsigned n=0;n<64;++n){setValue(p,Crossfade,n/63.);p.viewState.zoom[0]=1+(n%64);Bytes snapshot;
        check(saveState(plugin,&snapshot.out)&&snapshot.data.size()<30000,"mixer and zoom snapshots never rescan missing original files");
        audio.run(plugin);check(p.engine.active(0),"Motion keeps playing through state saves");}
    std::cout<<"Project media: 64 Motion/mixer/zoom snapshots in "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<" ms (includes audio blocks)\n";
    check(probe.registrations==registrationCount&&p.revision==revision,"state save never re-registers files or republishes audio");
    plugin->stop_processing(plugin);plugin->deactivate(plugin);
    ready.at=0;check(loadState(plugin,&ready.in),"project-relative state recalls after originals moved");
    check(p.document->decks[0].stack.count==17,"all seventeen stereo layers recalled");
    collect();
    // Destructive editing must never reuse a locator for the unedited PCM.
    auto edited=std::make_shared<Document>(*p.document);auto& l=edited->decks[0].layers[0];l.asset=tone(2,.73f);l.path.clear();analyzeLayer(l);publish(p,edited,1);
    Bytes editState;check(saveState(plugin,&editState.out)&&editState.data.size()>380000,"edited audio stays embedded, not replaced with stale project media");
    collect();const auto editedPath=p.projectMedia[0][0].copy.absolutePath;
    Bytes editedReady;check(saveState(plugin,&editedReady.out)&&editedReady.data.size()<30000,"generated PCM becomes a small verified project reference");
    std::shared_ptr<const SampleAsset> exported;std::string exportError;
    check(decode(editedPath,exported,exportError)&&exported->channels==l.asset->channels,"generated float WAV preserves exact audio");
    const auto undoRevision=p.revision;const auto undoCount=p.undo.size();
    collect();check(p.revision==undoRevision&&p.undo.size()==undoCount,"generated collection does not create source edits");
    history(p,false);Bytes undoState;check(saveState(plugin,&undoState.out)&&undoState.data.size()<30000,"undo immediately reuses the earlier verified audio version");
    history(p,true);Bytes redoState;check(saveState(plugin,&redoState.out)&&redoState.data.size()<30000,"redo immediately reuses generated media without exporting again");
    check(std::filesystem::exists(editedPath),"undo never deletes media referenced by host history");
    // Save As changes the storage domain: embed safely until the new worker
    // result is ready; no hash/copy is allowed inside either save call.
    probe.file=(root/"New Project"/"session.rpp").string();probe.media=(root/"New Project"/"Media").string();
    Bytes relocated;check(saveState(plugin,&relocated.out)&&relocated.data.size()>6000000,"new project domain cannot reuse old relative locators");
    check(!std::filesystem::exists(probe.media),"Save As snapshot does not collect synchronously");collect();
    Bytes collected;check(saveState(plugin,&collected.out)&&collected.data.size()<30000,"Save As collects imported and generated layers");
    collected.at=0;check(loadState(plugin,&collected.in)&&p.document->decks[0].layers[0].asset->channels==l.asset->channels,"generated media recalls from the new project");
    // Captures and edits share the immutable pathless-asset path. Cover all
    // output widths, phase-opposed channels, crop length and exact PCM recall.
    for(unsigned width:{2u,4u,8u,16u}){
        auto generated=std::make_shared<Document>();auto& take=generated->decks[0].layers[0];
        take.asset=tone(width,.43f);take.start=.125;take.end=.625;take.name="CAPTURE / CROP";
        take.asset=cropLayer(take);take.start=0;take.end=1;analyzeLayer(take);publish(p,generated,1,false);
        collect();Bytes small;check(saveState(plugin,&small.out)&&small.data.size()<30000,"multichannel take exports instead of growing snapshots");
        small.at=0;check(loadState(plugin,&small.in)&&p.document->decks[0].layers[0].asset->channels==take.asset->channels,"multichannel generated media keeps channel order and exact float PCM");
    }
    // Complete an old job after its layer has been replaced: it must never
    // overwrite the current asset or supply a locator for the wrong audio.
    auto stale=std::make_shared<Document>();auto& old=stale->decks[0].layers[0];old.asset=tone(2,.17f);old.name="OLD TAKE";analyzeLayer(old);publish(p,stale,1,false);
    p.mediaQueryAfter={};serviceProjectMedia(p);check(p.mediaWorker.valid(),"old generated collection launched");
    auto latest=std::make_shared<Document>(*stale);latest->decks[0].layers[0].asset=tone(2,.61f);analyzeLayer(latest->decks[0].layers[0]);publish(p,latest,1);
    collect();check(p.document->decks[0].layers[0].asset==latest->decks[0].layers[0].asset,"stale export never replaces a newer take");
    check(decode(p.projectMedia[0][0].copy.absolutePath,exported,exportError)&&exported->channels==latest->decks[0].layers[0].asset->channels,"latest take locator never points at superseded PCM");
    // Unsupported/unsaved project and failed export keep the only audio copy.
    probe.file.clear();Bytes unsaved;check(saveState(plugin,&unsaved.out)&&unsaved.data.size()>380000,"unsaved project safely embeds generated audio");
    probe.file=(root/"Failure"/"session.rpp").string();probe.media=doc->decks[0].layers[0].path+".away"; // Existing regular file, not a directory.
    collect();Bytes failed;check(saveState(plugin,&failed.out)&&failed.data.size()>380000,"failed project export keeps complete PCM");
    const auto failedRegistrationCount=probe.registrations;
    for(unsigned n=0;n<16;++n){Bytes repeated;check(saveState(plugin,&repeated.out)&&repeated.data==failed.data&&!p.mediaWorker.valid(),"failed-collection snapshots do not retry disk work");}
    check(probe.registrations==failedRegistrationCount,"failed saves never register nonexistent media");
    p.mediaQueryAfter={};serviceProjectMedia(p);check(!p.mediaWorker.valid(),"failed export backs off instead of retrying each UI tick");
    std::filesystem::rename(probe.media,probe.media+".blocked");p.projectMedia[0][0].retryAfter={};collect();
    Bytes recovered;check(saveState(plugin,&recovered.out)&&recovered.data.size()<30000,"background retry recovers generated media after the storage failure is resolved");
    plugin->destroy(plugin);check(probe.registrations==probe.unregistrations,"project registrations balanced");deckProjectProbe=nullptr;activeBridge=nullptr;
}
