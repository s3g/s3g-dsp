#include "s3g_tracker_windows_support.h"
#include "s3g/tracker/editor_shell.h"
#include "s3g/tracker/project_codec.h"
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

// Native Windows integration test: real DLL, HWNDs, VSTGUI input and a small
// REAPER-extension stub. This is not a substitute for acceptance in REAPER.
namespace {
using namespace s3g::tracker;
namespace w = s3g::tracker::windows;
int checks=0;
void require(bool ok,const char* label) {
    ++checks;
    if(!ok){std::cerr<<"FAIL: "<<label<<" (Win32 "<<GetLastError()<<")\n";std::exit(1);}
}
std::vector<w::Accelerator*> accelerators;
using Info=int (*)(HWND,INT_PTR);
Info info=nullptr;
int infoAdds=0,infoRemoves=0;
int registration(const char* name,void* value) {
    if(!std::strcmp(name,"hwnd_info")){info=reinterpret_cast<Info>(value);++infoAdds;return 1;}
    if(!std::strcmp(name,"-hwnd_info")){info=nullptr;++infoRemoves;return 1;}
    auto* a=static_cast<w::Accelerator*>(value);
    if(!std::strcmp(name,"<accelerator")||!std::strcmp(name,"accelerator")){
        accelerators.push_back(a);return 1;
    }
    if(!std::strcmp(name,"-accelerator")){
        accelerators.erase(std::remove(accelerators.begin(),accelerators.end(),a),accelerators.end());return 1;
    }
    return 0;
}
int stopped(){return 0;}
double tempo(){return 120;}
void* get(const char* name){
    if(!std::strcmp(name,"GetPlayState"))return reinterpret_cast<void*>(stopped);
    if(!std::strcmp(name,"Master_GetTempo"))return reinterpret_cast<void*>(tempo);
    return nullptr;
}
w::ReaperBridge bridge{1,nullptr,registration,get};
const void* extension(const clap_host_t*,const char* id){
    return !std::strcmp(id,"cockos.reaper_extension")?&bridge:nullptr;
}
void noop(const clap_host_t*){}
void pump() {
    MSG msg{};
    for(int i=0;i<1000&&PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);++i){
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
}
void click(HWND hwnd,double x,double y){
    const auto p=MAKELPARAM(int(x),int(y));
    SendMessageW(hwnd,WM_MOUSEMOVE,0,p);
    SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,p);
    SendMessageW(hwnd,WM_LBUTTONUP,0,p);pump();
}
void key(HWND hwnd,WPARAM vk){
    // VSTGUI handles keydown (including generic CTextEdit); real translated
    // character messages are pumped too, without changing the user's keyboard.
    PostMessageW(hwnd,WM_KEYDOWN,vk,1);
    PostMessageW(hwnd,WM_KEYUP,vk,LPARAM(0xc0000001));pump();
}
std::string save(const clap_plugin_t* plugin){
    auto* state=static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE));
    std::string result;
    clap_ostream_t stream{&result,[](const clap_ostream_t* s,const void* p,uint64_t n)->int64_t{
        // Force partial writes, as hosts are allowed to do.
        n=std::min<uint64_t>(n,113);static_cast<std::string*>(s->ctx)->append(static_cast<const char*>(p),n);return int64_t(n);
    }};
    require(state&&state->save(plugin,&stream),"CLAP state save");return result;
}
bool load(const clap_plugin_t* plugin,const std::string& text){
    struct Reader{const std::string* text;std::size_t pos=0;} r{&text};
    clap_istream_t stream{&r,[](const clap_istream_t* s,void* p,uint64_t n)->int64_t{
        auto& r=*static_cast<Reader*>(s->ctx);n=std::min<uint64_t>({n,97,r.text->size()-r.pos});
        std::memcpy(p,r.text->data()+r.pos,n);r.pos+=n;return int64_t(n);
    }};
    return static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin,CLAP_EXT_STATE))->load(plugin,&stream);
}
struct Instance {
    const clap_plugin_t* plugin;
    const clap_plugin_gui_t* gui;
    HWND parent=nullptr,root=nullptr,shell=nullptr;
    std::array<HWND,editor::kShellPageCount> pages{};
    explicit Instance(const clap_plugin_factory_t* factory,const clap_host_t* host){
        plugin=factory->create_plugin(factory,host,"org.s3g.s3g-dsp.tracker");
        require(plugin&&plugin->init(plugin),"create Tracker");
        gui=static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin,CLAP_EXT_GUI));
        require(gui&&gui->is_api_supported(plugin,CLAP_WINDOW_API_WIN32,false),"Win32 GUI advertised");
        require(!gui->is_api_supported(plugin,CLAP_WINDOW_API_COCOA,false)
            &&!gui->is_api_supported(plugin,CLAP_WINDOW_API_WIN32,true),"unsupported API/floating rejected");
    }
    void open(){
        parent=CreateWindowExW(0,L"STATIC",L"Tracker Windows test host",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            20,20,1360,940,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent!=nullptr,"host parent window");
        require(gui->create(plugin,CLAP_WINDOW_API_WIN32,false),"create ten-page editor");
        clap_window_t window{};window.api=CLAP_WINDOW_API_WIN32;window.win32=parent;
        require(gui->set_parent(plugin,&window)&&gui->show(plugin),"embed and show editor");
        root=FindWindowExW(parent,nullptr,L"S3GTrackerPortableWindow",nullptr);
        require(root!=nullptr,"Tracker root HWND");
        for(auto child=GetWindow(root,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
            wchar_t cls[256]{};GetClassNameW(child,cls,256);
            if(std::wcscmp(cls,L"S3GTrackerPortableWindow"))shell=child;
        }
        require(shell!=nullptr,"VSTGUI shell HWND");
        for(unsigned i=0;i<pages.size();++i){
            const auto title=w::wide(std::string("s3g Tracker page ")+editor::ShellController::title(editor::ShellPage(i)));
            pages[i]=FindWindowExW(root,nullptr,L"S3GTrackerPortableWindow",title.c_str());
            require(pages[i]&&GetWindow(pages[i],GW_CHILD),"each page has embedded VSTGUI frame");
        }
        SetForegroundWindow(parent);pump();
        checkEmbedded(0);
    }
    HWND contentWindow() const {
        RECT r{};GetClientRect(root,&r);
        // Parent-relative hit testing is independent of screen size, external
        // occlusion and the test host's clipping at 200% scale.
        return ChildWindowFromPointEx(root,{r.right/2,r.bottom/2},
            CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT);
    }
    void checkEmbedded(unsigned i) const {
        require(GetParent(pages[i])==root,"selected page remains embedded");
        require(IsWindowVisible(pages[i]),"selected page has visible ancestors");
        // IsWindowVisible alone passed even when the opaque shell covered the
        // entire page. Check the actual sibling exposed in the content area.
        require(contentWindow()==pages[i],"selected content is above the shell, not obscured");
        RECT r{};GetClientRect(root,&r);
        const LONG nav=LONG(std::lround(40.*r.bottom/860.));
        require(ChildWindowFromPointEx(root,{r.right/2,nav/2},
            CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT)==shell,
            "navigation remains exposed above embedded content");
        RECT pageBounds{};GetWindowRect(pages[i],&pageBounds);
        MapWindowPoints(nullptr,root,reinterpret_cast<POINT*>(&pageBounds),2);
        require(pageBounds.left==0&&pageBounds.top==nav
            &&pageBounds.right==r.right&&pageBounds.bottom==r.bottom,
            "embedded page occupies the scaled content area");
        const HWND frame=GetWindow(pages[i],GW_CHILD);
        RECT client{};GetClientRect(frame,&client);
        require(frame&&IsWindowVisible(frame)&&client.right>0&&client.bottom>0,
            "selected page has a visible nonempty VSTGUI frame");
        for(unsigned n=0;n<pages.size();++n)
            if(n!=i&&GetParent(pages[n])==root)
                require(!IsWindowVisible(pages[n]),"unselected embedded page stays hidden");
    }
    void select(unsigned i){
        const auto r=editor::ShellController{}.layout(1320,860).tabs[i];
        RECT bounds{};GetClientRect(root,&bounds);
        click(shell,(r.x+r.width*.5)*bounds.right/1320.,
            (r.y+r.height*.5)*bounds.bottom/860.);
        UpdateWindow(GetWindow(pages[i],GW_CHILD));pump();
        checkEmbedded(i);
    }
    void close(){
        gui->destroy(plugin);pump();
        require(!IsWindow(root),"GUI destroy removes root and child frames");
        DestroyWindow(parent);parent=root=shell=nullptr;
    }
    ~Instance(){if(root)close();plugin->destroy(plugin);}
};
}
int main(){
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    require(argv&&argc==2,"usage: s3g_tracker_windows_clap_smoke.exe <s3g_tracker.clap>");
    const std::wstring path=argv[1];LocalFree(argv);
    const auto utf8path=w::utf8(path);
    require(w::wide(w::utf8(L"C:\\Tracker 雨\\café.s3gt"))==L"C:\\Tracker 雨\\café.s3gt","UTF-8/UTF-16 roundtrip");
    HMODULE module=LoadLibraryW(path.c_str());require(module!=nullptr,"load Windows CLAP with system DLLs only");
    auto* entry=reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(module,"clap_entry"));
    require(entry&&entry->init(utf8path.c_str()),"CLAP entry initialized");
    auto* factory=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory&&factory->get_plugin_count(factory)==1,"Tracker factory");
    clap_host_t host{CLAP_VERSION_INIT,nullptr,"Tracker Win32 smoke host","s3g","","1",extension,noop,noop,noop};
    {
        Instance a(factory,&host);
        ProjectDocument initial;
        require(decodeProjectDocument(save(a.plugin),initial).ok(),"decode initial state");
        // Legacy zero-ID rows gain stable identities on first editor open on
        // Mac too. Use an already-identified project for exact recall checks.
        for(std::size_t i=0;i<initial.song.rows.size();++i)initial.song.rows[i].id=uint32_t(i+1);
        std::string saved;
        require(encodeProjectDocument(initial,saved).ok(),"encode identified fixture");
        require(load(a.plugin,saved)&&save(a.plugin)==saved,"closed GUI exact state roundtrip");
        require(!load(a.plugin,"not a project")&&save(a.plugin)==saved,"bad state is rejected without mutation");
        a.open();
        require(save(a.plugin)==saved,"opening editor does not alter stored project");
        // A deliberate occlusion proves the check distinguishes visibility
        // flags from exposed content. Selecting the same page must repair it.
        require(SetWindowPos(a.pages[0],HWND_BOTTOM,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"occlusion positive control setup");
        require(IsWindowVisible(a.pages[0])&&a.contentWindow()!=a.pages[0],
            "occlusion check detects a visible page hidden behind shell");
        a.select(0);
        uint32_t width=1,height=1;
        require(a.gui->adjust_size(a.plugin,&width,&height)&&width==858&&height==559,"65 percent minimum");
        width=9999;height=9999;
        require(a.gui->adjust_size(a.plugin,&width,&height)&&width==2640&&height==1720,"200 percent maximum");
        for(auto size: {std::pair{858u,559u},{1320u,860u},{1980u,1290u},{2640u,1720u},{1320u,860u}}){
            require(a.gui->set_size(a.plugin,size.first,size.second),"proportional resize");
            require(a.gui->get_size(a.plugin,&width,&height)&&width==size.first&&height==size.second,"reported dimensions stable");pump();
            // Tracker/Song cannot use detachment as a visibility workaround.
            a.select(0);a.select(1);
        }
        // No page may rely on an earlier detach/reattach to establish order.
        for(unsigned i=0;i<a.pages.size();++i)a.select(i);
        for(unsigned i=0;i<a.pages.size();++i){
            a.select(i);
            if(!editor::ShellController::canDetach(editor::ShellPage(i)))continue;
            click(a.shell,1290,20);
            const auto floating=GetParent(a.pages[i]);
            require(floating!=a.root&&GetWindow(floating,GW_OWNER)==a.parent,"detached page owned by FX host");
            SetWindowPos(floating,nullptr,50,50,780,560,SWP_NOZORDER|SWP_NOACTIVATE);pump();
            require(GetWindow(a.pages[i],GW_CHILD)!=nullptr,"detached VSTGUI frame survives resize");
            SendMessageW(floating,WM_CLOSE,0,0);pump();
            require(GetParent(a.pages[i])==a.root&&!IsWindow(floating),"close detached page reattaches without destruction");
            a.checkEmbedded(i);
        }
        for(unsigned i: {9u,0u,7u,1u,2u,0u})a.select(i);
        a.select(1);
        require(a.gui->hide(a.plugin)&&a.gui->show(a.plugin),"Song hide/show lifecycle");
        pump();a.checkEmbedded(1);
        for(unsigned i: {0u,8u}){
            a.select(i);const auto frame=GetWindow(a.pages[i],GW_CHILD);
            click(frame,120,i==0?90:48);
            require(info&&info(GetFocus(),0)==1&&info(GetFocus(),1)==1,"live-code field classified as text by REAPER hook");
            MSG space{};space.hwnd=GetFocus();space.message=WM_KEYDOWN;space.wParam=VK_SPACE;
            require(accelerators.size()==1&&accelerators[0]->translate(&space,accelerators[0])==-1,
                "space routed to focused editor, not REAPER transport");
            // Both fields submit through the original command engine.
            for(char c:std::string(i==0?"len 1 16":"len 1 32"))key(GetFocus(),c==' '?VK_SPACE:WPARAM(std::toupper(c)));
            key(GetFocus(),VK_RETURN);
            ProjectDocument d;require(decodeProjectDocument(save(a.plugin),d).ok(),"decode edited state");
            require(d.patternBank.findEntry(d.patternBank.activePatternId)->pattern.tracks[0].noteColumn.length==(i==0?16u:32u),
                "command containing spaces executes from each live-code field");
        }
        require(load(a.plugin,saved)&&save(a.plugin)==saved,"state recall with editor open");
        a.checkEmbedded(8);
        {
            Instance b(factory,&host);b.open();
            require(infoAdds==1&&accelerators.size()==2,"multiple instances share hwnd_info registration");
            b.select(0);auto* own=accelerators.back();auto* other=accelerators.front();
            MSG m{};m.hwnd=GetFocus();m.message=WM_KEYDOWN;m.wParam=VK_SPACE;
            require(own->translate(&m,own)==-1&&other->translate(&m,other)==0,"focus routing isolated between instances");
            a.close();
            require(info&&infoRemoves==0&&accelerators.size()==1,"closing one instance preserves sibling hooks");
            SetFocus(b.parent);m.hwnd=b.parent;
            require(own->translate(&m,own)==0,"host focus does not get intercepted");
            require(b.gui->hide(b.plugin)&&b.gui->show(b.plugin),"hide/show lifecycle");
            pump();b.checkEmbedded(0);
        }
        require(!info&&accelerators.empty()&&infoRemoves==1,"last editor removes all hooks");
        a.open();a.close();require(!info&&accelerators.empty(),"reopen/close cleans runtime and hooks again");
        require(a.plugin->activate(a.plugin,48000,1,512)&&a.plugin->start_processing(a.plugin),"CLAP MIDI engine activation");
        unsigned output=0;
        clap_output_events_t sink{&output,[](const clap_output_events_t* o,const clap_event_header_t* e){
            require(e->time<512,"MIDI event inside audio block");++*static_cast<unsigned*>(o->ctx);return true;
        }};
        clap_event_transport_t transport{};transport.header.size=sizeof(transport);transport.header.type=CLAP_EVENT_TRANSPORT;
        transport.flags=CLAP_TRANSPORT_IS_PLAYING|CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE;transport.tempo=120;
        clap_process_t process{};process.frames_count=512;process.transport=&transport;process.out_events=&sink;
        for(unsigned i=0;i<64;++i){transport.song_pos_beats=clap_beattime(double(i)*512*2/48000*CLAP_BEATTIME_FACTOR);
            require(a.plugin->process(a.plugin,&process)!=CLAP_PROCESS_ERROR,"MIDI processing with GUI closed");}
        a.plugin->stop_processing(a.plugin);a.plugin->deactivate(a.plugin);
    }
    entry->deinit();FreeLibrary(module);
    std::cout<<checks<<" Windows integration checks passed\n";
}
