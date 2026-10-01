namespace decks_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation=s3g::portable_gui::foundation;
using F=NeonFamily;
constexpr double panelWidth=DeckLayout::panelWidth, rowPitch=DeckLayout::rowPitch; // shared ordinary toolbox pitch
class Editor final:public View {
public:
    explicit Editor(Plugin& plugin):View(kGuiWidth,kGuiHeight,true),p(plugin),page(plugin.viewState.page),view(plugin.viewState.view),zoom(plugin.viewState.zoom),offset(plugin.viewState.offset){setWantsFocus(true);}
    ~Editor() override {endCutPatternDrag();for(unsigned d=0;d<2;++d){if(guiTouch[d])command(p,{CommandKind::Touch,d,0,0});
        for(unsigned n=0;n<8;++n)command(p,{CommandKind::ScreenPad,d,n,-1});}}
    void service() override {::service(p);}
    static double left(unsigned d){return DeckLayout::left(d);}
    static CRect wave(unsigned d){return rect(left(d)+16,DeckLayout::waveTop,panelWidth-32,DeckLayout::waveHeight);}
    CRect pathBounds(unsigned d,bool editing=false) const {return editing?rect(left(1-d)+16,228,panelWidth-32,DeckLayout::editorBottom-228):rect(left(d)+16,DeckLayout::pathTop,panelWidth-32,DeckLayout::pathHeight);}
    void paint() override {
        foundation::drawPluginTitle(*context,"s3g Sample Decks 32",rect(18,12,270,20),titleFont);
        button(rect(328,13,68,18),"LOAD SET",[this]{setFile(false);});
        button(rect(404,13,68,18),"SAVE SET",[this]{setFile(true);});
        button(rect(490,13,52,18),"UNDO",[this]{history(p,false);},false,!p.undo.empty()&&!blocked());
        button(rect(550,13,52,18),"REDO",[this]{history(p,true);},false,!p.redo.empty()&&!blocked());
        button(rect(620,13,100,18),"REFRESH LEDS",[this]{p.refreshLeds.store(true);request(p);p.message="LED FEEDBACK → TRACK MIDI HARDWARE OUTPUT → BEATPAD 2";});
        button(rect(736,13,100,18),"HEADPHONE CUE",[this]{const unsigned d=static_cast<unsigned>(value(p,FocusDeck));p.viewState.oppositeEdit[1-d]=1;page[d]=7;p.dirty.store(true);});
        text(foundation::peakDbText(p.outputPeak.load()),rect(kGuiWidth-190,12,172,20),style.value,kRightText);
        for(unsigned d=0;d<2;++d){(void)p.playbackVisuals[d].read(visual[d]);deck(d);}
        mixer();
        text(p.message,rect(18,DeckLayout::footerTop,kGuiWidth-290,18),style.dim);
        text(format("AUDIO %.1f / 512 MiB",p.loadedAudioBytes/double(1024*1024)),rect(kGuiWidth-262,DeckLayout::footerTop,244,18),style.dim,kRightText);
    }
    void onMouseDownEvent(MouseDownEvent& e) override {if(getFrame())getFrame()->setFocusView(this);View::onMouseDownEvent(e);}
    void onKeyboardEvent(KeyboardEvent& e) override {
        if(e.type!=EventType::KeyDown||!getFrame()||getFrame()->getFocusView()!=this)return;
        if(e.modifiers.has(ModifierKey::Super)||e.modifiers.has(ModifierKey::Control)){
            if(e.character=='z'||e.character=='Z'){history(p,e.modifiers.has(ModifierKey::Shift));e.consumed=true;}
        }
    }
    bool dropped(const std::vector<std::string>& paths,CPoint point) override {if(paths.empty()||point.x<left(0)||(point.x>left(0)+panelWidth&&point.x<left(1))||point.x>left(1)+panelWidth)return false;loadFiles(p,p.viewState.target(point.x>=left(1)?1:0),paths);return true;}
    void wheel(MouseWheelEvent& e) override {
        for(unsigned d=0;d<2;++d)if(!p.viewState.oppositeEdit[d]&&foundation::contains(wave(d),e.mousePosition)){
            const double old=zoom[d];const auto before=viewport(d);
            const bool horizontal=std::abs(e.deltaX)>std::abs(e.deltaY)||e.modifiers.has(ModifierKey::Shift);
            if(horizontal){if(p.viewState.scrolling(d)&&view[d]!=1&&view[d]!=3)return;
                offset[d]=before.pan(e.deltaX!=0?e.deltaX:e.deltaY);
            }else{
                const double t=p.viewState.vertical(d)?(e.mousePosition.y-wave(d).top)/wave(d).getHeight():(e.mousePosition.x-wave(d).left)/wave(d).getWidth();
                zoom[d]=std::clamp(old*std::pow(1.3,e.deltaY),1.,64.);
                offset[d]=std::clamp(before.at(t)-t/zoom[d],0.,1-1/zoom[d]);
            }e.consumed=true;invalid();}
    }
    bool pointerDown(MouseDownEvent& e) override {
        if(e.buttonState.isRight()){
            for(unsigned side=0;side<2;++side)if(p.viewState.oppositeEdit[side]){
                const unsigned d=p.viewState.target(side);
                for(unsigned n=0;n<32;++n)if(foundation::contains(layerCell(left(side),DeckLayout::gridTop,n),e.mousePosition)){
                    const bool loaded=bool(p.document->decks[d].layers[n].asset);
                    openPopup(rect(e.mousePosition.x,e.mousePosition.y,164,0),{"COPY LAYER","PASTE LAYER","CLEAR LAYER"},-1,
                        [this,d,n](int choice){if(choice==1)requestLayerPaste(d,n);else editStackLayer(p,d,n,choice==0?LayerEdit::Copy:LayerEdit::Clear);},
                        1,{},18,{loaded&&!blocked(),bool(p.layerClipboard)&&!blocked(),loaded&&!blocked()});return true;
                }
            }
        }
        for(unsigned d=0;d<2;++d)if(!p.viewState.oppositeEdit[d]&&view[d]==3&&foundation::contains(wave(d),e.mousePosition))return sliceDown(d,e);
        for(unsigned side=0;side<2;++side){const bool editing=p.viewState.oppositeEdit[side]!=0;const unsigned d=p.viewState.target(side);
            if(editing&&page[d]==5&&e.buttonState.isLeft()&&foundation::contains(cutPatternBounds(d),e.mousePosition)){
                endCutPatternDrag();cutDragDeck=d;cutPatternDrag=true;cutPatternSourceDrag=e.modifiers.has(ModifierKey::Alt);
                editCutPatternAt(e.mousePosition,e.clickCount==2);if(e.clickCount==2)endCutPatternDrag();return true;}
            if(editing&&page[d]!=2)continue;
            const auto b=pathBounds(d,editing);if(!foundation::contains(b,e.mousePosition))continue;
            auto f=pathSettings(d);const float t=static_cast<float>((e.mousePosition.x-b.left)/b.getWidth()),v=static_cast<float>((e.mousePosition.y-b.top)/b.getHeight());
            if(e.buttonState.isRight()){unsigned best=32;float dist=.035f;for(unsigned i=1;i+1<unsigned(f[F::PathCount]);++i){const float delta=std::abs(f.values[neonFamilyIndex(F::PathTime)+i]-t);if(delta<dist){dist=delta;best=i;}}
                if(best<32){neonRemoveStackPoint(f,best);storePath(d,f);}return true;}
            if(e.buttonState.isLeft()){dragDeck=d;dragBounds=b;dragPoint=neonAddStackPoint(f,t,v,.025f);storePath(d,f);return true;}
        }return false;
    }
    void pointerMove(MouseMoveEvent& e) override {
        if(sliceDrag){moveSlice(e.mousePosition);e.consumed=true;return;}
        if(cutPatternDrag){
            if(!p.viewState.oppositeEdit[1-cutDragDeck]||page[cutDragDeck]!=5){endCutPatternDrag();return;}
            editCutPatternAt(e.mousePosition);e.consumed=true;return;}
        if(dragPoint<0)return;auto f=pathSettings(dragDeck);const auto b=dragBounds;
        neonMoveStackPoint(f,static_cast<unsigned>(dragPoint),static_cast<float>((e.mousePosition.x-b.left)/b.getWidth()),static_cast<float>((e.mousePosition.y-b.top)/b.getHeight()));storePath(dragDeck,f);e.consumed=true;
    }
    void pointerUp(MouseUpEvent&) override {dragPoint=-1;endCutPatternDrag();if(sliceDrag)finishSlice();}
private:
    static CRect layerCell(double x,double y,unsigned n){return rect(x+16+(n%8)*72,y+(n/8)*21,66,18);}
    void requestLayerPaste(unsigned d,unsigned n){
        if(blocked()||!p.layerClipboard)return;
        if(!p.document->decks[d].layers[n].asset){editStackLayer(p,d,n,LayerEdit::Paste);return;}
        openPopup(rect(left(1-d)+172,DeckLayout::gridTop-28,260,0),{format("REPLACE %c / LAYER %02u?",'A'+d,n+1),"CANCEL","PASTE / REPLACE"},-1,
            [this,d,n](int choice){if(choice==2)editStackLayer(p,d,n,LayerEdit::Paste);},1,{},18,{false,true,true});
    }
    // Zoom/pan are view preferences, serialized on the next normal save. They
    // must not create host undo snapshots of the entire sample set.
    CTextEdit* makeNumericTextEdit(CRect bounds,const char* text) override {
        // Readouts are compact, but the editor shows all nine significant
        // digits. Grow left over the slider, never outside the row/panel.
        bounds.left=bounds.right-std::max(112.,bounds.getWidth());
        const auto center=(bounds.top+bounds.bottom)*.5;
        bounds.top=center-11;bounds.bottom=center+11;
#if defined(__APPLE__)
        auto* field=s3g::portable_gui::makeReadableNumericTextEdit(bounds,this,1,text);
#else
        auto* field=View::makeNumericTextEdit(bounds,text);
#endif
        field->setTextInset({4,0});
        return field;
    }
    void deckPanel(CRect bounds,const std::string& title,unsigned deck){
        // Gray-based ownership tints; 24px inside the top rule centers 18px
        // buttons with 3px above/below. Match the title to the same centerline.
        foundation::drawPanel(*context,bounds,title,font,26,8,8.5,color(deck?0x51423c:0x3b4c4e),style.label);
    }
    bool blocked() const {return p.busy||p.capture.state.load()!=0;}
    unsigned layer(unsigned d) const {return static_cast<unsigned>(control(p,d,Layer));}
    void gesture(unsigned id,bool begin){s3g::clap_gui::enqueueParamEvent(p.guiParams,p.host,p.hostParams,begin?s3g::clap_gui::ParamEventKind::GestureBegin:s3g::clap_gui::ParamEventKind::GestureEnd,id+1);}
    void set(unsigned id,double v){
        if(id>=kDeckBase){const unsigned d=(id-kDeckBase)/kDeckStride,key=(id-kDeckBase)%kDeckStride;
            if(key==family(F::StackShape)){auto f=pathSettings(d);neonSetStackShape(f,static_cast<NeonStackShape>(static_cast<unsigned>(v)),static_cast<unsigned>(f[F::PathCount]),d);storePath(d,f);return;}
            if(key==Layer){if(blocked())return;guiValue(p,param(d,StackNavigation),0);const auto& l=p.document->decks[d].layers[static_cast<unsigned>(v)];guiValue(p,param(d,Start),l.start);guiValue(p,param(d,End),l.end);}
            if(key==StackPosition)guiValue(p,param(d,StackNavigation),1);
            if(key==ScreenLatch&&v==0)for(unsigned n=0;n<8;++n)command(p,{CommandKind::ScreenPad,d,n,-1});
            if(value(p,Format)>=3&&((key==Character&&v>=6)||(key==Playback&&v==5))){p.message="WAVESETS, DRIVE AND CRUSH ARE DISABLED FOR AMBISONIC SOURCES";return;}
            if(key==Start)v=std::min(v,double(control(p,d,End))-.000001);
            if(key==End)v=std::max(v,double(control(p,d,Start))+.000001);
        }
        guiValue(p,id,v);
    }
    void controlRow(unsigned id,double x,double y,double w=276,const std::string& custom={}) {
        const auto& def=p.defs[id];if(def.name.empty())return;const double v=value(p,id);
        const auto label=custom.empty()?def.name:custom;
        const bool compact=w<180;
        if(!compact||def.choices.empty())text(label,rect(x,y-2,compact?30:w*.49,18),style.label);
        const auto box=compact?rect(x+(def.choices.empty()?34:0),y-3,w-(def.choices.empty()?34:0),19):rect(x+w*.5,y-3,w*.5,19);
        const unsigned key=id>=kDeckBase?(id-kDeckBase)%kDeckStride:kDeckStride;
        const unsigned d=id>=kDeckBase?(id-kDeckBase)/kDeckStride:0;
        if(key>=kFx&&key<kFx+8*kNeonCharacterControls){
            const unsigned effect=(key-kFx)/kNeonCharacterControls,n=(key-kFx)%kNeonCharacterControls;
            const auto& fx=kNeonCharacterDefs[effect][n];
            if(*fx.choices){std::vector<std::string> options;std::string all=fx.choices;std::size_t begin=0;
                for(;;){const auto end=all.find('|',begin);options.push_back(all.substr(begin,end-begin));if(end==std::string::npos)break;begin=end+1;}
                const unsigned chosen=static_cast<unsigned>(neonCharacterDisplay(effect,n,v));
                menu(box,options[chosen],options,static_cast<int>(chosen),[this,id,effect,n](int c){
                    if(!neonCharacterControlAllowed(effect,n,c,value(p,Format)>=3)){p.message="THIS EFFECT MODE IS DISABLED FOR AMBISONICS";return;}
                    set(id,neonCharacterNormalized(effect,n,c));});return;
            }
        }
        if((key==Technique1&&control(p,d,Playback)==3)||(key==Technique2&&control(p,d,Playback)==5)){
            const bool sequence=control(p,d,Playback)==3;
            const std::vector<std::string> options=sequence?std::vector<std::string>{"FORWARD","REVERSE","RANDOM"}:
                std::vector<std::string>{"REPEAT","OMIT","REPLACE","ENVELOPE","MULTIPLY","AVERAGE","INTERPOLATE","FRACTAL","HARMONIC","GROUP REVERSE","CYCLE REVERSE","TELESCOPE"};
            const unsigned chosen=sequence?(v>.75?2:v>=.25?1:0):static_cast<unsigned>(std::round(v*11));
            menu(box,options[chosen],options,chosen,[this,id,sequence](int n){set(id,sequence?n*.5:n/11.);});return;
        }
        if(!def.choices.empty()){
            const unsigned choice=static_cast<unsigned>(std::clamp(std::round(v),def.lo,def.hi));
            const std::string prefix=!compact?"":id==Stems?"STEMS: ":id==Curve?"FADE: ":id==Storage?"STORE: ":id==FocusDeck?"FOCUS: ":"";
            menu(box,prefix+def.choices[choice],def.choices,static_cast<int>(choice),[this,id](int n){gesture(id,true);set(id,n);gesture(id,false);});return;
        }
        const double valueWidth=compact?44:64,valueGap=6;
        const auto numberBox=rect(box.right-valueWidth,y-3,valueWidth,20);
        const auto track=rect(box.left,y+7,numberBox.left-valueGap-box.left,1);
        foundation::drawHorizontalSlider(*context,track,(v-def.lo)/std::max(.000001,def.hi-def.lo),y,15);
        const bool fx=key>=kFx&&key<kFx+8*kNeonCharacterControls;
        const unsigned effect=fx?(key-kFx)/kNeonCharacterControls:0,n=fx?(key-kFx)%kNeonCharacterControls:0;
        const double display=fx?neonCharacterDisplay(effect,n,v):v;
        text(def.integer?format("%.0f",display):format("%.3g",display),rect(numberBox.left+4,numberBox.top,numberBox.getWidth()-8,numberBox.getHeight()),style.value,kRightText);
        drag(rect(track.left-4,y-4,track.getWidth()+8,22),[this,id,track](CPoint point){const auto& f=p.defs[id];set(id,f.lo+std::clamp((point.x-track.left)/track.getWidth(),0.,1.)*(f.hi-f.lo));},
            [this,id]{gesture(id,true);},[this,id]{gesture(id,false);trimCommit(id);},[this,id]{gesture(id,true);set(id,p.defs[id].initial);gesture(id,false);trimCommit(id);});
        number(numberBox,display,[this,id,fx,effect,n](double v){set(id,fx?neonCharacterNormalized(effect,n,v):v);},true,[this,id]{gesture(id,true);},[this,id]{gesture(id,false);trimCommit(id);});
    }
    void trimCommit(unsigned id){if(id<kDeckBase)return;const unsigned d=(id-kDeckBase)/kDeckStride,key=(id-kDeckBase)%kDeckStride;
        if(key!=Start&&key!=End)return;if(blocked())return;auto doc=std::make_shared<Document>(*p.document);auto& l=doc->decks[d].layers[layer(d)];
        const double first=control(p,d,Start),last=control(p,d,End);
        setValue(p,param(d,Start),l.start);setValue(p,param(d,End),l.end);
        l.start=first;l.end=last;doc->action="TRIM";publish(p,std::move(doc),1u<<d);
        guiValue(p,param(d,Start),first);guiValue(p,param(d,End),last);}
    void load(unsigned d){foundation::FileDialogOptions o;o.title="Add audio layers to Deck "+std::string(d?"B":"A");o.extension="wav";o.extensionDescription="Audio";
        const auto path=foundation::runFileDialog(getFrame(),o);if(!path.empty())loadFiles(p,d,{path});}
    void setFile(bool save){foundation::FileDialogOptions o;o.save=save;o.title=save?"Save Sample Decks set":"Load Sample Decks set";o.extension="s3gdecks";o.extensionDescription="Sample Decks Set";o.defaultSaveName="Sample Decks.s3gdecks";
        const auto path=foundation::runFileDialog(getFrame(),o);if(path.empty())return;
        const bool ok=save?s3g::clap_gui::portable::saveStateFile(&p.plugin,stateExtension,path.c_str()):s3g::clap_gui::portable::loadStateFile(&p.plugin,stateExtension,path.c_str());
        p.message=ok?(save?"SET SAVED":"SET LOADED"):"SET OPERATION FAILED / FINISH CURRENT TAKE FIRST";}
    void edit(unsigned d,unsigned action){
        if(blocked())return;auto doc=std::make_shared<Document>(*p.document);auto& deck=doc->decks[d];auto& l=deck.layers[layer(d)];if(!l.asset)return;
        if(action==0){l={};doc->action="DELETE LAYER";}
        else if(action==1){l.start=control(p,d,Start);l.end=control(p,d,End);
            for(auto& cue:deck.bookmarks)if(cue.set&&cue.layer==layer(d)){if(cue.position<l.start||cue.position>=l.end)cue={};else cue.position=(cue.position-l.start)/(l.end-l.start);}
            l.asset=cropLayer(l);l.path.clear();l.start=0;l.end=1;l.slices=equalSampleNeonSliceLayout(8);analyzeLayer(l);doc->action="CROP TO SELECTION";}
        else if(action==2){auto a=std::make_shared<SampleAsset>(*l.asset);float peak=0;for(unsigned ch=0;ch<a->channelCount;++ch)for(float v:a->channels[ch])peak=std::max(peak,std::abs(v));
            if(peak>0)for(unsigned ch=0;ch<a->channelCount;++ch)for(float& v:a->channels[ch])v*=.89125094f/peak;l.asset=a;l.path.clear();analyzeLayer(l);doc->action="NORMALIZE LAYER";}
        else if(action==3||action==4){const auto n=static_cast<unsigned>(control(p,d,SliceCount));
            std::vector<float> transients;for(unsigned i=0;i<l.analysis.transientRegions.count;++i){const double t=l.analysis.transientRegions.starts[i];if(t>l.start&&t<l.end)transients.push_back(static_cast<float>((t-l.start)/(l.end-l.start)));}
            l.slices=action==3?equalSampleNeonSliceLayout(n):transientSampleNeonSliceLayout(transients.data(),transients.size(),n);
            if(control(p,d,ZeroCross)!=0)for(unsigned i=1;i<l.slices.sliceCount;++i)snapDeckSliceMarker(l,l.slices,i);
            doc->action="SET SLICE MARKERS";}
        else if(action==5){unsigned available=0;for(const auto& item:deck.layers)if(!item.asset)++available;if(available<l.slices.sliceCount){p.message="NOT ENOUGH EMPTY LAYERS FOR SLICES";return;}
            const auto source=l;for(unsigned i=0;i<source.slices.sliceCount;++i){LayerAudio cut=source;cut.start=source.start+(source.end-source.start)*source.slices.boundaries[i];cut.end=source.start+(source.end-source.start)*source.slices.boundaries[i+1];
                cut.asset=cropLayer(cut);cut.path.clear();cut.name=source.name+" SLICE "+std::to_string(i+1);cut.start=0;cut.end=1;cut.slices=equalSampleNeonSliceLayout(1);analyzeLayer(cut);deck.layers[deck.firstEmpty()]=std::move(cut);}doc->action="SLICES TO NEW LAYERS";}
        if(audioBytes(*doc)>kAudioBudget){p.message="AUDIO BUDGET EXCEEDED";return;}publish(p,std::move(doc),1u<<d);
        if(action==1||action==0){guiValue(p,param(d,Start),0);guiValue(p,param(d,End),1);}
        p.message=p.document->action;
    }
    void deck(unsigned side){
        const double x=left(side);const unsigned d=p.viewState.target(side);const bool editing=p.viewState.oppositeEdit[side]!=0;
        const auto& doc=p.document->decks[d];const auto& l=doc.layers[layer(d)];
        deckPanel(rect(x,42,panelWidth,DeckLayout::deckHeight),editing?(d?"EDIT DECK B":"EDIT DECK A"):(d?"DECK B":"DECK A"),d);
        button(rect(x+148,47,204,18),side?"B WAVE":"A WAVE",[this,side]{p.viewState.oppositeEdit[side]=0;p.dirty.store(true);},!editing);
        button(rect(x+362,47,226,18),side?"A EDIT":"B EDIT",[this,side]{p.viewState.oppositeEdit[side]=1;p.dirty.store(true);},editing);
        if(editing){
            button(rect(x+486,75,102,19),"SHOW WAVE",[this,d]{p.viewState.oppositeEdit[d]=0;p.dirty.store(true);});
            menu(rect(x+16,75,270,19),pages[page[d]],{pages.begin(),pages.end()},page[d],[this,d](int i){page[d]=i;subpage[d]=0;p.dirty.store(true);});
            constexpr unsigned fieldsPerPage=32;
            auto fields=controls(d);const unsigned count=std::max(1u,(static_cast<unsigned>(fields.size())+fieldsPerPage-1)/fieldsPerPage);subpage[d]=std::min(subpage[d],count-1);
            if(count>1){std::vector<std::string> choices;for(unsigned n=0;n<count;++n)choices.push_back("CONTROLS "+std::to_string(n+1));
                menu(rect(x+312,75,164,19),choices[subpage[d]],choices,subpage[d],[this,d](int n){subpage[d]=n;});}
            const unsigned shown=std::min(fieldsPerPage,static_cast<unsigned>(fields.size())-std::min(static_cast<unsigned>(fields.size()),subpage[d]*fieldsPerPage));
            const unsigned rows=std::max(1u,(shown+1)/2);
            for(unsigned i=0;i<shown;++i){const auto key=fields[subpage[d]*fieldsPerPage+i];controlRow(param(d,key),x+16+(i/rows)*296,104+(i%rows)*rowPitch,276,techniqueLabel(d,key));}
            if(page[d]==2)stackPath(d,pathBounds(d,true));
            else if(page[d]==5)cutPattern(d);
            else if(page[d]==0){
                const char* labels[]{"CROP SELECTION","NORMALIZE","DELETE LAYER","EQUAL SLICES","TRANSIENT SLICES","SLICES TO LAYERS"};
                const unsigned actions[]{1,2,0,3,4,5};for(unsigned n=0;n<6;++n)button(rect(x+16+(n%3)*192,206+(n/3)*32,180,20),labels[n],[this,d,n,actions]{edit(d,actions[n]);},false,l.asset&&!blocked());
                button(rect(x+16,276,180,20),"EDIT SLICE MARKERS",[this,d]{view[d]=3;zoom[d]=1;offset[d]=0;p.viewState.oppositeEdit[d]=0;p.dirty.store(true);},view[d]==3,l.asset&&!blocked());
                button(rect(x+208,276,180,20),"CLEAR MARKERS",[this,d]{setSliceLayout(d,equalSampleNeonSliceLayout(1));},false,l.asset&&!blocked());
                text("CLICK ADD / DRAG MOVE / RIGHT CLICK REMOVE",rect(x+16,310,572,18),style.dim);
            }else if(page[d]==6)screenPerformance(d,x);
            else if(page[d]==7)headphonePanel(x);
            layerGrid(d,x,DeckLayout::gridTop);
        }else{
            controlRow(param(d,Playback),x+16,78,276);
            const unsigned method=static_cast<unsigned>(control(p,d,Playback));
            controlRow(param(d,method==6?family(F::LaneAuto):method==8?family(F::CutFileOrder):method==3?family(F::MosaicScope):SourceMode),x+312,78,276);
            std::vector<std::string> layers;for(unsigned n=0;n<32;++n)layers.push_back(format("%02u  %s",n+1,doc.layers[n].asset?doc.layers[n].name.c_str():"EMPTY"));
            text("LAYER",rect(x+16,102,52,18),style.label);menu(rect(x+74,101,296,19),layers[layer(d)],layers,static_cast<int>(layer(d)),[this,d](int i){set(param(d,Layer),i);},!blocked());
            button(rect(x+382,101,96,19),"ADD AUDIO",[this,d]{load(d);},false,!blocked());
            button(rect(x+486,101,102,19),"ZOOM RESET",[this,d]{zoom[d]=p.viewState.scrolling(d)?4:1;offset[d]=0;});
            menu(rect(x+16,130,180,19),views[view[d]],{views.begin(),views.end()},view[d],[this,d](int n){view[d]=n;if(n==3){zoom[d]=1;offset[d]=0;}p.dirty.store(true);});
            const std::vector<std::string> channels{"ALL CHANNELS","COMBINED PEAKS","CHANNEL 1"};
            menu(rect(x+206,130,182,19),view[d]==2?"STACK PEAKS":channels[p.viewState.channels[d]],channels,p.viewState.channels[d],[this,d](int n){p.viewState.channels[d]=n;p.dirty.store(true);},view[d]!=2);
            const std::vector<std::string> orientations{"VERTICAL SCROLL","HORIZONTAL SCROLL","HORIZONTAL OVERVIEW","VERTICAL OVERVIEW"};
            menu(rect(x+398,130,190,19),orientations[p.viewState.orientation[d]],orientations,p.viewState.orientation[d],[this,d](int n){p.viewState.orientation[d]=n;if(n>=2){zoom[d]=1;offset[d]=0;}p.dirty.store(true);});
            waveform(d);
            text("STACK PATH",rect(x+16,DeckLayout::pathTop-26,100,18),style.label);
            controlRow(param(d,StackNavigation),x+124,DeckLayout::pathTop-23,218);
            controlRow(param(d,StackPosition),x+358,DeckLayout::pathTop-23,230,"STACK POS");
            stackPath(d,pathBounds(d));
        }
        performance(side);
    }
    void performance(unsigned d){const double x=left(d),y=DeckLayout::performanceTop;deckPanel(rect(x,y,panelWidth,111),d?"B / PERFORMANCE":"A / PERFORMANCE",d);
        button(rect(x+148,y+5,64,18),p.playing[d].load()?"PAUSE":"PLAY",[this,d]{command(p,{CommandKind::Play,d});},p.playing[d].load());
        button(rect(x+220,y+5,64,18),"STOP",[this,d]{command(p,{CommandKind::Stop,d});});
        button(rect(x+292,y+5,80,18),"SET CUE",[this,d]{command(p,{CommandKind::SetCue,d});});
        button(rect(x+380,y+5,64,18),"CUE",[this,d]{command(p,{CommandKind::Cue,d});});
        button(rect(x+452,y+5,64,18),"SYNC",[this,d]{command(p,{CommandKind::Sync,d});});
        button(rect(x+524,y+5,64,18),"INPUT",[this,d]{p.viewState.oppositeEdit[1-d]=1;page[d]=6;p.dirty.store(true);});
        controlRow(param(d,PadMode),x+16,y+36,276);
        const bool keyboard=keyboardPads(p,d);const unsigned mode=static_cast<unsigned>(control(p,d,PadMode));
        if(mode==4){const unsigned role=static_cast<unsigned>(control(p,d,SamplerPads));
            menu(rect(x+312,y+33,132,19),role?"KEYBOARD":"LAYERS",{"LAYERS","KEYBOARD"},role,[this,d](int n){set(param(d,SamplerPads),n);});
            const unsigned bank=static_cast<unsigned>(control(p,d,PadBank));
            const std::string prefix=keyboard?"KEYS ":"LAYERS ";std::vector<std::string> choices;
            for(unsigned b=0;b<4;++b)choices.push_back(prefix+format("%u–%u",b*8+1,b*8+8));
            menu(rect(x+456,y+33,132,19),choices[bank],choices,bank,[this,d](int n){set(param(d,PadBank),n);});
        }else if(mode==3||mode==6){const auto& banks=p.defs[param(d,PadBank)].choices;const unsigned bank=static_cast<unsigned>(control(p,d,PadBank));
            if(mode==3){const auto& choices=p.defs[param(d,SliceHold)].choices;const unsigned selected=static_cast<unsigned>(control(p,d,SliceHold));
                menu(rect(x+312,y+33,132,19),choices[selected],choices,selected,[this,d](int n){set(param(d,SliceHold),n);});}
            else text("STACK BANK",rect(x+312,y+34,128,18),style.label);
            menu(rect(x+456,y+33,132,19),banks[bank],banks,bank,[this,d](int n){set(param(d,PadBank),n);});
        }else if(mode==0||mode==2){const unsigned key=mode==0?CueAction:FxLatch,selected=static_cast<unsigned>(control(p,d,key));const auto& choices=p.defs[param(d,key)].choices;
            menu(rect(x+312,y+33,132,19),choices[selected],choices,selected,[this,d,key](int n){set(param(d,key),n);});
            text(mode==0?"LAYER + POSITION":"PRESSURE → AMOUNT",rect(x+456,y+34,132,18),style.dim);
        }else text(mode==1?"SOURCE BEATS / TOGGLE":mode==5?"OUTPUT BEATS / HOLD":value(p,RecordSource)!=0?"TRACK INPUT → THIS DECK":"THIS DECK → OTHER DECK",rect(x+312,y+34,276,18),style.label);
        for(unsigned n=0;n<8;++n){const auto b=rect(x+16+n*72,DeckLayout::padTop,66,31);const unsigned index=(mode==3||mode==4||mode==6?static_cast<unsigned>(control(p,d,PadBank))*8:0)+n;
            const int key=keyboardNote(p,d,n);
            const bool loaded=padAvailable(p,*p.document,d,mode,static_cast<unsigned>(control(p,d,PadBank)),n);
            const char* captures[]{"1 BEAT","2 BEATS","4 BEATS","8 BEATS","RECORD","TAKE","AUDITION","LAUNCH"};
            const char* names[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            const auto label=keyboard?(key<0||key>127?std::string("OFF"):format("%s%d",names[key%12],key/12-1)):
                mode==2?p.defs[param(d,Character)].choices[n]:mode==0?format("CUE %u",n+1):mode==1?std::string(kLoopLabels[n]):mode==5?std::string(kRollLabels[n]):mode==7?std::string(n==4&&p.capture.state.load()!=0?"STOP":captures[n]):format("%u%s",index+1,loaded?" •":"");
            const auto state=PadFeedback::unpack(p.padFeedback[d][n].load(std::memory_order_relaxed));
            const bool matched=state.matches(mode,static_cast<unsigned>(control(p,d,PadBank)),keyboard);
            const bool pressed=matched&&(state.held||state.pulse);
            const unsigned lights=p.performanceLights[d].load();const bool latched=(mode==1&&(lights&(1u<<n)))||(mode==2&&(lights&(1u<<(8+n))))||(mode==5&&(lights&(1u<<(16+n))))
                ||(mode==7&&n==4&&(p.capture.state.load()==1||p.capture.state.load()==2)&&p.capture.from==d);
            const bool selected=latched||(loaded&&mode==4&&(keyboard?key%12==int(control(p,d,KeyboardRoot))%12:layer(d)==index));
            buttonFace(b,label,pressed||selected,loaded||pressed||latched);
            if(pressed){stroke(b,color(d?0xc3a394:0x99bdc1),2);if(state.held)fill(rect(b.left+3,b.bottom-4,(b.getWidth()-6)*state.pressure,2),style.text);}
            if(loaded||pressed||latched)drag(b,[](CPoint){},[this,d,n,b,label]{flashButton(b,label);command(p,{CommandKind::ScreenPad,d,n,1});},[this,d,n]{command(p,{CommandKind::ScreenPad,d,n,0});});}
        if(mode==3)text(control(p,d,SliceRaw)!=0?"RAW SAMPLE AUDITION":"USES SELECTED PLAYBACK METHOD",rect(x+16,y+95,572,14),style.dim);
    }
    void layerGrid(unsigned d,double x,double y){text(format("DECK %c / STACK LAYERS",'A'+d),rect(x+16,y-22,300,18),style.label);
        for(unsigned n=0;n<32;++n){const auto b=layerCell(x,y,n);const bool loaded=bool(p.document->decks[d].layers[n].asset);
            button(b,format("%02u%s",n+1,loaded?" •":""),[this,d,n]{set(param(d,Layer),n);},n==layer(d),!blocked());
            if(layerWeight(d,n)>.01f)fill(rect(b.left,b.bottom-2,b.getWidth()*layerWeight(d,n),2),style.value);
        }
    }
    std::string techniqueLabel(unsigned d,unsigned key){if(key<Technique0||key>Technique3)return {};const unsigned method=static_cast<unsigned>(control(p,d,Playback));
        if(method==3){const char* names[]{"STEP RATE","STEP ORDER","REPEATS","CHANCE"};return names[key-Technique0];}
        if(method==5){const char* names[]{"GROUP","REPEATS","SHAPE","PROCESS"};return names[key-Technique0];}return {};}
    std::vector<unsigned> controls(unsigned d){const auto f=[](F key){return family(key);};
        if(page[d]==7)return {};
        if(page[d]==5)return {};
        if(page[d]==6)return {KeyboardRoot,KeyboardOctave,KeyboardVoices,ScreenVelocity,ScreenPressure,ScreenLatch,CueAction,SliceHold,SliceRaw,FxLatch,RideTime};
        if(page[d]==0)return {Start,End,ZeroCross,SliceCount,SourceBpm};
        if(page[d]==2){std::vector<unsigned> r{StackCycle,StackBeats,f(F::StackShape),f(F::StackJump),f(F::StackCurve),f(F::PathCount),f(F::StackOffset)};
            if(control(p,d,Playback)==2)r.push_back(f(F::StackAdvance));return r;}
        if(page[d]==3){std::vector<unsigned> r{Character,PressureDepth};const unsigned effect=static_cast<unsigned>(control(p,d,Character));
            for(unsigned n=0;n<kNeonCharacterCounts[effect];++n)r.push_back(kFx+effect*kNeonCharacterControls+n);r.push_back(kFx+effect*kNeonCharacterControls+11);return r;}
        if(page[d]==4)return {f(F::RoutingMode),f(F::RoutingWidth),f(F::RoutingTraversal),f(F::GrainStereoLink),CuePosition};
        std::vector<unsigned> r{Rate,Tune,Direction,Repeat,Clock,Position,Attack,Release};
        auto add=[&](std::initializer_list<unsigned> list){r.insert(r.end(),list.begin(),list.end());};
        switch(static_cast<unsigned>(control(p,d,Playback))){
        case 0:break;
        case 1:add({Cycle,CycleBeats,MotionPath,f(F::MotionTrajectory),f(F::MotionLocus),f(F::MotionField),f(F::MotionTravel),f(F::MotionJitter),f(F::MotionWindow),f(F::MotionSound),f(F::PacketRate),f(F::PacketDuty),f(F::MotorRate),f(F::MotorShape),f(F::MotorSymmetry),f(F::MotionModel),f(F::EventRate),f(F::EventRepeats),f(F::EventStep),f(F::EventPitch),f(F::EventLevel),f(F::EventCurve)});break;
        case 2:add({GrainSize,Density,Spray,PitchSpray,ReverseChance,f(F::GrainSource),f(F::GrainProcess),f(F::GrainAmount),f(F::GrainRegions),f(F::GrainWindow),f(F::GrainSkew),f(F::GrainSizeVariation),f(F::GrainLevelVariation),f(F::GrainScatter),f(F::GrainBias),f(F::GrainPitch),f(F::GrainTimeSync),f(F::GrainSizeScale),f(F::GrainDensityScale),Cycle,CycleBeats,MotionPath});break;
        case 3:add({Technique0,Technique1,Technique2,Technique3,f(F::SliceAttack),f(F::SliceDecay),f(F::SliceSustain),f(F::SliceRelease),f(F::MosaicMode),f(F::MosaicTarget)});break;
        case 4:add({Cycle,CycleBeats});break;
        case 5:add({f(F::WavesetEngine),Technique0,Technique1,Technique2,Technique3,f(F::OscPosition),f(F::OscFrequency),f(F::OscScan),f(F::OscGroup)});break;
        case 6:add({f(F::LanePosition),f(F::LaneRate),f(F::LaneSlew),f(F::LaneJoin)});break;
        case 7:add({f(F::SpectralBlur),f(F::SpectralAdvance),f(F::SpectralPressure),f(F::SpectralSmear),f(F::SpectralFocus),f(F::SpectralTilt),f(F::SpectralThin),Cycle,CycleBeats});break;
        case 8:for(unsigned i=neonFamilyIndex(F::CutDivision);i<neonFamilyIndex(F::CutPatternLane);++i)if(i!=neonFamilyIndex(F::CutFileOrder))r.push_back(kFamily+i);break;
        }return r;
    }
#include "s3g_sample_decks_visual.inc"
#include "s3g_sample_decks_slice_gui.inc"
#include "s3g_sample_decks_cutups_gui.inc"
    NeonFamilySettings pathSettings(unsigned d) const {NeonFamilySettings f;for(unsigned i=0;i<kNeonFamilyCount;++i)f.values[i]=control(p,d,kFamily+i);return f;}
    void storePath(unsigned d,const NeonFamilySettings& f){for(unsigned i=0;i<kNeonFamilyCount;++i)if(control(p,d,kFamily+i)!=f.values[i])guiValue(p,param(d,kFamily+i),f.values[i]);}
    void headphonePanel(double x){
        const unsigned width=sampleNeonBusWidth(decksLayout(static_cast<unsigned>(value(p,Format))));
        const unsigned selected=static_cast<unsigned>(value(p,HeadphoneOutput));const bool stems=value(p,Stems)!=0;
        const int output=deckCueOutput(selected,width,stems);const auto& choices=p.defs[HeadphoneOutput].choices;
        std::vector<bool> enabled;for(unsigned n=0;n<choices.size();++n)enabled.push_back(!n||deckCueOutput(n,width,stems)>=0);
        text("CUE OUTPUT",rect(x+16,102,136,18),style.label);
        const std::string label=selected==1&&output>=0?format("AUTO → %d–%d",output+1,output+2):choices[selected];
        const auto box=rect(x+154,101,138,19);foundation::drawMenuBox(*context,box,label,font,style);
        hit(box,[this,box,choices,selected,enabled]{openPopup(box,choices,selected,[this](int n){set(HeadphoneOutput,n);},1,{},18,enabled);});
        if(value(p,Format)>=3){text("QUAD / OCTO FOLD",rect(x+312,102,136,18),style.dim);
            menu(rect(x+450,101,138,19),"AMBI → XY",{},-1,[](int){},false);}
        else controlRow(HeadphoneFold,x+312,104,276);
        text(value(p,Format)>=3?"AMBI: ACN/SN3D → XY STEREO":"AMBI: AUTO XY DECODE",rect(x+312,128,276,18),style.dim);
        text("STEREO MONITOR — NOT HRTF / BINAURAL",rect(x+312,154,276,18),style.dim);
        const std::string status=!selected?"HEADPHONE CUE OFF":output<0?"CUE MUTED: OUTPUT OVERLAPS MAIN / STEMS":format("CUE → PLUG-IN OUT %d–%d",output+1,output+2);
        text(status,rect(x+16,222,572,18),style.label);
        text("ROUTE THIS PAIR TO HEADPHONES IN REAPER",rect(x+16,248,572,18),style.dim);
        text("CUE IS AFTER EQ / FX, BEFORE FADERS",rect(x+16,274,572,18),style.dim);
    }
    void mixer(){
        using M=DeckMixerLayout;const double x=DeckLayout::centerLeft,w=DeckLayout::centerWidth;
        panel(rect(x,42,w,106),"OUTPUT");
        controlRow(Output,x+16,78,w-32);controlRow(Format,x+16,104,w-32);
        controlRow(Stems,x+16,130,M::stripWidth);controlRow(Curve,M::stripLeft(1),130,M::stripWidth);
        panel(rect(x,160,w,447),"MIXER");
        // These are the same CLAP values used by the Beatpad adapter and host
        // automation, never an editor-side copy of the hardware's mixer state.
        constexpr unsigned toneKeys[]{Gain,High,Mid,Low,Filter,Amount};
        const char* toneLabels[]{"GAIN","HIGH","MID","LOW","FILT","FX"};
        for(unsigned d=0;d<2;++d){const double xx=M::stripLeft(d);
            text(d?"DECK B":"DECK A",rect(xx,184,M::stripWidth,18),style.label,kCenterText);
            for(unsigned n=0;n<6;++n)controlRow(param(d,toneKeys[n]),xx,M::toneRow(n),M::stripWidth,toneLabels[n]);
            button(rect(xx,M::cueTop,M::stripWidth,18),d?"CUE B":"CUE A",[this,d]{gesture(HeadphoneA+d,true);set(HeadphoneA+d,1-value(p,HeadphoneA+d));gesture(HeadphoneA+d,false);},value(p,HeadphoneA+d)!=0);
            button(rect(xx,M::slipTop,M::stripWidth,18),"SLIP",[this,d]{const auto id=param(d,Slip);gesture(id,true);set(id,1-value(p,id));gesture(id,false);},control(p,d,Slip)!=0);
            const auto area=rect(xx+(M::stripWidth-32)*.5,M::faderTop,32,M::faderHeight);
            fill(area,style.cell);stroke(area,style.grid);
            const double y=area.bottom-control(p,d,Level)*area.getHeight();fill(rect(area.left-3,y-2,38,4),style.value);
            const auto id=param(d,Level);
            drag(area,[this,id,area](CPoint pt){set(id,std::clamp((area.bottom-pt.y)/area.getHeight(),0.,1.));},
                [this,id]{gesture(id,true);},[this,id]{gesture(id,false);},
                [this,id]{gesture(id,true);set(id,p.defs[id].initial);gesture(id,false);});
            const auto valueBox=rect(xx+38,M::levelTop,64,18);
            text(format("%.3g",value(p,id)),valueBox,style.value,kCenterText);
            number(valueBox,value(p,id),[this,id](double v){set(id,v);},true,[this,id]{gesture(id,true);},[this,id]{gesture(id,false);});
        }
        text("A ← FADE → B",rect(x+16,M::crossLabel,w-32,17),style.label,kCenterText);
        const auto cross=rect(x+16,M::crossTrack,w-32,1);foundation::drawHorizontalSlider(*context,cross,value(p,Crossfade),M::crossBaseline,15);
        drag(rect(x+12,M::crossBaseline-3,w-24,23),[this,cross](CPoint point){set(Crossfade,(point.x-cross.left)/cross.getWidth());},
            [this]{gesture(Crossfade,true);},[this]{gesture(Crossfade,false);},
            [this]{gesture(Crossfade,true);set(Crossfade,p.defs[Crossfade].initial);gesture(Crossfade,false);});
        controlRow(HeadphoneMix,x+16,M::cueMixRow,w-32);controlRow(HeadphoneLevel,x+16,M::cueLevelRow,w-32);
        panel(rect(x,M::recordTop,w,124),"RECORD / RESAMPLE");
        const bool record=p.capture.state.load()==1||p.capture.state.load()==2;
        const auto source=static_cast<unsigned>(value(p,RecordSource));const bool recordInput=source!=0;
        const auto& sources=p.defs[RecordSource].choices;
        menu(rect(x+16,M::recordSourceRow-3,M::stripWidth,19),sources[source],sources,source,[this](int n){set(RecordSource,n);},!blocked());
        const unsigned width=sampleNeonBusWidth(decksLayout(static_cast<unsigned>(value(p,Format))));
        if(recordInput){std::vector<std::string> groups;for(unsigned first=0;first+width<=32;first+=width)groups.push_back(format("INPUT %u–%u",first+1,first+width));
            const unsigned group=recordInputFirst(p,width)/width;
            menu(rect(M::stripLeft(1),M::recordSourceRow-3,M::stripWidth,19),groups[group],groups,group,[this](int n){set(RecordInputGroup,n);},!blocked());
        }else text("DECK → NEW LAYER",rect(M::stripLeft(1),M::recordSourceRow-2,M::stripWidth,18),style.dim,kCenterText);
        for(unsigned d=0;d<2;++d){const bool active=record&&p.capture.from==d;
            const std::string destination=recordInput?std::string("INPUT → ")+(d?"B":"A"):(d?"B → A":"A → B");
            const unsigned to=recordInput?d:1-d,target=active?p.capture.target:p.document->decks[to].recordTarget(layer(to));
            button(rect(M::stripLeft(d),M::recordButtonTop,M::stripWidth,20),(active?"STOP ":"REC ")+destination+(target<32?format(" L%02u",target+1):" FULL"),[this,d]{startRecord(p,d);},active,!p.busy&&(!record||active));}
        const auto r=rect(x+16,M::recordPreviewTop,178,16);fill(r,style.cell);stroke(r,style.grid);
        for(unsigned n=0;n<178;++n){const float pk=p.capture.peaks[n*512/178].load();line({r.left+n,r.top+8-std::min(1.f,pk)*6},{r.left+n,r.top+8+std::min(1.f,pk)*6},style.value);}
        text(format("%.1f /",p.capture.frames.load()/std::max(1.,p.capture.rate)),rect(x+202,M::recordLimitTop,50,18),style.dim,kRightText);
        const auto maxBox=rect(x+260,M::recordLimitTop,48,18);text(format("%.0f S",value(p,RecordSeconds)),maxBox,style.value);
        number(maxBox,value(p,RecordSeconds),[this](double v){set(RecordSeconds,v);},true,[this]{gesture(RecordSeconds,true);},[this]{gesture(RecordSeconds,false);});
        panel(rect(x,M::setupTop,w,106),"SETUP");
        std::vector<std::string> inputs{"BEATPAD 2","NOTES / ALL"};for(unsigned i=1;i<=16;++i)inputs.push_back("NOTES / CH "+std::to_string(i));
        const int input=value(p,Controller)?0:1+static_cast<int>(value(p,MidiChannel));
        menu(rect(x+16,M::setupRow-3,w-32,19),inputs[input],inputs,input,[this](int n){set(Controller,n==0?1:0);set(MidiChannel,n>0?n-1:0);});
        controlRow(Storage,x+16,M::setupRow+rowPitch,w-32);controlRow(FocusDeck,x+16,M::setupRow+2*rowPitch,w-32);
    }
    Plugin& p;
    std::array<uint8_t,2>& page;std::array<uint8_t,2>& view;std::array<unsigned,2> subpage {};
    std::array<double,2>& zoom;std::array<double,2>& offset;
    std::array<NeonVisualSnapshot,2> visual {};
    CRect dragBounds;
    DeckViewport dragViewport;
    std::array<bool,2> guiTouch {};
    int dragPoint=-1;unsigned dragDeck=0;
    const std::array<std::string,8> pages {{"SOURCE / CHOP","PLAYBACK","STACK PATH","CHARACTER FX","MIXER / ROUTING","CUTUPS PATTERN","PERFORMANCE INPUT","HEADPHONE CUE"}};
    const std::array<std::string,4> views {{"FOLLOW STACK","EDIT LAYER","STACK LANES","EDIT SLICES"}};
};
} // namespace decks_canvas
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p){return new decks_canvas::Editor(p);}
