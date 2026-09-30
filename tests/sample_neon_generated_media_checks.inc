void testGeneratedAuditionMedia(){
    using namespace generated_test;Project project;auto instance=historyFixture();auto& p=*instance;
    p.host=&project.host;p.storageMode=StorageMode::Project;
    for(unsigned channels:{2u,4u,8u,16u}){
        const auto original=audio(channels);p.captureAsset=original;
        const auto pending=historyState(p);check(pending.size()>48000*channels*4,"Neon pending audition embeds");
        collect([&]{serviceStorage(p);},[&]{return !p.generatedMedia.reference(original,p.host).empty();});
        const auto ready=historyState(p);check(ready.size()<pending.size()/2,"Neon audition is reference-only");
        for(unsigned n=0;n<32;++n)check(historyState(p)==ready,"Neon repeated reference snapshot stable");
        sample_snapshot_test::Buffer loaded;loaded.bytes=ready;
        check(stateLoad(&p.plugin,&loaded.input),"Neon v28 capture reference loads");
        check(p.captureAsset&&p.captureAsset->channels==original->channels,"Neon capture exact multichannel PCM recall");
        check(historyState(p)==ready,"Neon immediate save after recall stays compact");
        p.storageMode=StorageMode::Embed;check(historyState(p).size()>ready.size(),"Neon explicit Embed preserves PCM");p.storageMode=StorageMode::Project;
    }
    const auto previous=p.captureAsset;auto next=audio(2,.63f);p.captureAsset=next;
    serviceStorage(p);p.captureAsset=audio(2,.81f);const auto latest=p.captureAsset;
    collect([&]{serviceStorage(p);},[&]{return !p.generatedMedia.reference(latest,p.host).empty();});
    check(p.captureAsset==latest,"stale Neon audition export never changes newer take");
    p.captureAsset=previous;check(historyState(p).size()<48000*16*4,"undo uses earlier audition reference");
    project.file.clear();check(historyState(p).size()>48000*16*4,"unsaved project embeds audition");
    project.file=(project.root/"Second"/"Session.rpp").string();project.directory=(project.root/"Second"/"Media").string();
    check(historyState(p).size()>48000*16*4,"Save As cannot reuse old audition locator");
    collect([&]{serviceStorage(p);},[&]{return !p.generatedMedia.reference(previous,p.host).empty();});
    check(historyState(p).size()<48000*16*4,"Save As recollects audition off save thread");
    const auto previousReference=p.generatedMedia.reference(previous,p.host);std::string blocked;
    check(s3g::sample_storage::resolveProjectRelativePath(s3g::sample_storage::reaperContext(p.host),previousReference,blocked),"resolve failure fixture");
    project.file=(project.root/"Failure.rpp").string();project.directory=blocked; // A WAV cannot be a media directory.
    p.captureAsset=audio(2,.37f);const auto callbackCount=project.callbacks.load();serviceStorage(p);
    collect([&]{serviceStorage(p);},[&]{return project.callbacks.load()>callbackCount;});serviceStorage(p);
    const auto failed=historyState(p);check(failed.size()>48000*2*4,"failed audition export retains safety PCM");
    for(unsigned n=0;n<32;++n)check(historyState(p)==failed,"failed export snapshots remain stable");
    sample_snapshot_test::Buffer fallback;fallback.bytes=failed;check(stateLoad(&p.plugin,&fallback.input),"embedded fallback reloads after export failure");
    check(p.captureAsset&&p.captureAsset->channelCount==2,"failed export fallback keeps audition");
    instance.reset();check(project.additions==project.removals,"Neon generated registrations balanced");
}
