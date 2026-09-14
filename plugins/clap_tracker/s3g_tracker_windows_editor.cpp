#include "s3g_tracker_windows_editor.h"
#include "s3g/tracker/clap_editor_coordinator.h"
#include "s3g_tracker_main_page.h"
#include "s3g_tracker_shell_view.h"
#include "s3g_tracker_song_page.h"
#include "s3g_tracker_geometry_page.h"
#include "s3g_tracker_authoring_page.h"
#include "s3g_tracker_warp_page.h"
#include "s3g_tracker_reference_page.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/platform/iplatformstring.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "s3g_tracker_windows_support.h"
#include <commctrl.h>
#include <map>
#include <set>
#include <sstream>

namespace s3g::tracker {
namespace {
using namespace VSTGUI;
using namespace editor;
namespace f = portable_gui::foundation;
namespace w = windows;
constexpr UINT kSelectPage = WM_APP + 70, kDetachPage = WM_APP + 71;
constexpr wchar_t kWindowClass[] = L"S3GTrackerPortableWindow";
unsigned windowClassUsers = 0;
HINSTANCE moduleInstance()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&windowClassUsers), &module);
    return module;
}
double now()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
Color literalColor(uint32_t rgb, double alpha)
{
    return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb),
        uint8_t(std::clamp(std::lround(alpha * 255), 0l, 255l))};
}
SharedPointer<CFontDesc> fontFactory(const std::string& name, double size)
{
    return makeOwned<CFontDesc>(name.c_str(), size);
}
GridFont metrics(std::string family, double size)
{
    // Like the Mac bridge, cache scalar metrics instead of reopening a font
    // for every visible grid cell. No platform font survives runtime shutdown.
    static thread_local std::map<std::pair<std::string,double>,GridFont> cache;
    const auto key=std::make_pair(family,size);
    if(auto found=cache.find(key);found!=cache.end())return found->second;
    auto font = fontFactory(family, size);
    auto p = font->getPlatformFont();
    const double ascent = p && p->getAscent() > 0 ? p->getAscent() : size;
    const double height = p ? std::ceil(ascent + std::max(0., p->getDescent())
        + std::max(0., p->getLeading())) : std::ceil(size * 1.3);
    if(cache.size()>=512)cache.clear();
    return cache.emplace(key,GridFont{std::move(family),size,ascent,height}).first->second;
}
double measure(std::string_view text, const GridFont& font)
{
    auto face = fontFactory(font.name, font.size);
    auto p = face->getPlatformFont();
    auto value = getPlatformFactory().createString(std::string(text).c_str());
    return p && p->getPainter() && value
        ? p->getPainter()->getStringWidth({}, value) : text.size() * font.size * .6;
}
struct Prompt {
    std::wstring title, initial;
    std::optional<std::string> result;
};
INT_PTR CALLBACK promptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* prompt = reinterpret_cast<Prompt*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        prompt = reinterpret_cast<Prompt*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, lp);
        SetWindowTextW(hwnd, prompt->title.c_str());
        RECT r {}; GetClientRect(hwnd, &r);
        auto edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", prompt->initial.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 12, r.right-24, 26,
            hwnd, reinterpret_cast<HMENU>(100), moduleInstance(), nullptr);
        CreateWindowExW(0,L"BUTTON",L"Rename",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            r.right-188,52,82,26,hwnd,reinterpret_cast<HMENU>(IDOK),moduleInstance(),nullptr);
        CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            r.right-94,52,82,26,hwnd,reinterpret_cast<HMENU>(IDCANCEL),moduleInstance(),nullptr);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return FALSE;
    }
    if (msg == WM_COMMAND && prompt) {
        if (LOWORD(wp) == IDOK) {
            auto edit = GetDlgItem(hwnd,100);
            std::wstring value(GetWindowTextLengthW(edit)+1, 0);
            const auto length = GetWindowTextW(edit,value.data(),int(value.size()));
            value.resize(length);
            prompt->result = w::utf8(value);
            EndDialog(hwnd, IDOK); return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
    }
    return FALSE;
}
std::optional<std::string> promptText(HWND owner, const std::string& title,
    const std::string& initial)
{
    struct Template {
        DLGTEMPLATE dialog;
        WORD menu = 0, windowClass = 0, title = 0;
    } value {};
    value.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    value.dialog.cx = 240; value.dialog.cy = 60;
    Prompt prompt {w::wide(title),w::wide(initial),{}};
    DialogBoxIndirectParamW(moduleInstance(),&value.dialog,owner,promptProc,
        reinterpret_cast<LPARAM>(&prompt));
    return prompt.result;
}
}

struct WindowsTrackerEditor::Impl : IKeyboardHook {
    struct WindowContext { Impl* owner = nullptr; int page = -1; bool floating = false; };
    struct Page {
        WindowContext context, floatingContext;
        HWND window = nullptr, floating = nullptr;
        std::unique_ptr<f::EditorHost> host;
        f::ContentView* view = nullptr;
    };
    midi::Engine& engine;
    const clap_host_t* clapHost;
    HWND root = nullptr, nativeParent = nullptr;
    WindowContext rootContext {this,-1,false};
    std::array<Page,kShellPageCount> pages;
    ShellController shell;
    ShellView* shellView = nullptr;
    std::unique_ptr<f::EditorHost> shellHost;
    std::unique_ptr<ClapEditorCoordinator> coordinator;
    MainPageView* main = nullptr;
    SongPageView* song = nullptr;
    GeometryPageView* geometry = nullptr;
    GeometryPageView* bursts = nullptr;
    std::array<AuthoringPageView*,3> authoring {};
    WarpPageView* warp = nullptr;
    ReferencePageView* console = nullptr;
    ReferencePageView* help = nullptr;
    uint32_t width = 1320, height = 860;
    bool runtime = false, registeredClass = false, visible = false, ready = false;
    bool closing = false, refreshing = false;
    GridPaintServices paint;
    std::string gridFamily, mediumFamily, boldFamily, suiteFamily;
    const w::ReaperBridge* reaper = nullptr;
    w::Accelerator accelerator;
    bool acceleratorRegistered = false, infoRegistered = false;
    static std::vector<Impl*>& instances() { static std::vector<Impl*> list; return list; }

    explicit Impl(midi::Engine& value, const clap_host_t* host) : engine(value),clapHost(host)
    {
        runtime = f::acquireRuntime();
        if (!runtime) return;
        WNDCLASSW cls {};
        cls.lpfnWndProc = windowProc; cls.hInstance = moduleInstance();
        cls.lpszClassName = kWindowClass; cls.hCursor = LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        cls.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if (!windowClassUsers && !RegisterClassW(&cls)) return;
        ++windowClassUsers; registeredClass = true;
        root = CreateWindowExW(0,kWindowClass,L"s3g Tracker",WS_POPUP|WS_CLIPCHILDREN,
            0,0,width,height,nullptr,nullptr,moduleInstance(),&rootContext);
        if (!root) return;
        std::set<std::string> families;
        getPlatformFactory().getAllFontFamilies([&](const std::string& name) {
            families.insert(name); return true;
        });
        gridFamily = families.count("IBM Plex Mono") ? "IBM Plex Mono" : f::selectedFontFamily();
        if (gridFamily.empty()) gridFamily = "Consolas";
        mediumFamily = families.count("IBM Plex Mono Medm") ? "IBM Plex Mono Medm" : gridFamily;
        boldFamily = families.count("IBM Plex Mono SmBld") ? "IBM Plex Mono SmBld" : mediumFamily;
        suiteFamily = f::selectedFontFamily().empty() ? gridFamily : f::selectedFontFamily();
        paint.font = [this](double size, FontWeight weight, bool) {
            const double readable = std::max(8.,std::round(size*1.16*2)/2);
            return metrics(weight == FontWeight::Semibold ? boldFamily
                : weight == FontWeight::Medium ? mediumFamily : gridFamily,readable);
        };
        paint.color = literalColor;
        song = new SongPageView(tools(1));
        pages[1].view = song;
        ClapEditorServices services;
        services.refresh = [this] { refresh(); };
        services.refreshPlayback = [this] { refreshPlayback(); };
        services.focusTracker = [this] { focusPage(0); if(main)main->focusTracker(); };
        services.showPage = [this](ShellPage page) {
            if(root)PostMessageW(root,kSelectPage,WPARAM(page),0);
        };
        services.hostPlaying = [this] { return w::playing(clapHost); };
        services.hostTempo = [this] { return w::tempo(clapHost); };
        services.hostToggle = [this] { return w::transport(clapHost,0,engine.visualPlaying); };
        services.hostContinue = [this] { return w::transport(clapHost,1,engine.visualPlaying); };
        services.hostStop = [this] { return w::transport(clapHost,2,engine.visualPlaying); };
        services.fileDialog = [this](bool save,const std::string& title,
            const std::string& extension,const std::string& name) {
            f::FileDialogOptions options;
            options.save=save; options.title=title; options.extension=extension;
            options.extensionDescription=extension=="s3gt" ? "Tracker Song + Patterns" : "Tracker Phrase + Burst Pack";
            options.defaultSaveName=name;
            return f::runFileDialog(activeFrame(), options);
        };
        services.promptText = [this](const auto& title,const auto& initial) {
            return promptText(activeWindow(),title,initial);
        };
        coordinator=std::make_unique<ClapEditorCoordinator>(engine,song->editor,std::move(services));
        auto& state=coordinator->state();
        auto& callbacks=coordinator->callbacks();
        GeometryServices gs;
        gs.paint=paint; gs.suiteFont=[this](double size){return metrics(suiteFamily,size);};
        gs.measure=measure;
        gs.capHeight=[](const GridFont& font) {
            auto fnt=fontFactory(font.name,font.size);
            auto p=fnt->getPlatformFont();
            return p&&p->getCapHeight()>0?p->getCapHeight():font.size*.72;
        };
        gs.error=[]{MessageBeep(MB_OK);}; gs.monotonicTime=now;
        gs.revealTracker=[this]{ selectPage(ShellPage::Tracker); };
        gs.focus=[this]{focusPage(2);};
        geometry=new GeometryPageView(state,callbacks,false,gs,fontFactory);
        pages[2].view=geometry;
        gs.focus=[this]{focusPage(3);};
        bursts=new GeometryPageView(state,callbacks,true,gs,fontFactory);
        pages[3].view=bursts;
        for(unsigned i=0;i<3;++i) {
            AuthoringServices as;
            as.tools=tools(4+i); as.grid=paint;
            as.error=[]{MessageBeep(MB_OK);};
            as.clipboardRevision=[]{return uint64_t(GetClipboardSequenceNumber());};
            as.monotonicTime=now;
            authoring[i]=new AuthoringPageView(state,callbacks,AuthoringPage(i),as);
            pages[4+i].view=authoring[i];
        }
        WarpPageServices ws;
        ws.font=gs.suiteFont;ws.color=literalColor;ws.fontFactory=fontFactory;
        ws.error=gs.error;ws.requestNativeFocus=[this]{focusPage(7);};
        warp=new WarpPageView(state,callbacks,ws);pages[7].view=warp;
        auto rs=[this](unsigned index) {
            ReferencePageServices result;result.tools=tools(index);
            result.font=[this](const ReferenceStyle& style) {
                return paint.font(style.size,style.weight,false);
            };
            result.returnToTracker=result.closeHelp=[this]{selectPage(ShellPage::Tracker);};
            return result;
        };
        console=new ReferencePageView(coordinator->console,rs(8));pages[8].view=console;
        help=new ReferencePageView(nullptr,rs(9));pages[9].view=help;
        MainPageServices ms;
        ms.paint=paint;ms.fontFactory=fontFactory;ms.suiteFont=gs.suiteFont;
        ms.requestNativeFocus=[this]{focusPage(0);};
        auto model=coordinator->console;
        ms.consoleDraft=[model]{return model->draft;};
        ms.consoleHistory=[model]{return model->history;};
        ms.consoleDraftChanged=[model](const auto& text){model->draft=text;};
        ms.submitConsole=[model](const auto& text){model->submit(text);};
        ms.consoleMessage=[this](const auto& text){coordinator->message(text);};
        ms.grid.clipboardRevision=[]{return uint64_t(GetClipboardSequenceNumber());};
        ms.grid.invalidEdit=gs.error;
        ms.grid.message=[this](const auto& text){
            coordinator->message(text);
            MessageBoxW(activeWindow(),w::wide(text).c_str(),L"Tracker",MB_OK);
        };
        ms.grid.capturePhrase=[this](std::size_t t,std::size_t first,std::size_t last) {
            if(authoring[0]->phrases.capture(t,first,last))selectPage(ShellPage::Phrases);
            refresh();
        };
        ms.grid.placePhrase=[this](std::size_t t,std::size_t row,bool merge) {
            authoring[0]->phrases.place(t,row,merge);refresh();
        };
        ms.pitchContour=[this](PitchContour contour,std::size_t first,std::size_t last) {
            geometry->editor().applyPitchMapContour(contour,first,last);refresh();
        };
        ms.openPitchMap=[this](std::size_t first,std::size_t last) {
            geometry->editor().openPitchMapFirstRow(first,last);selectPage(ShellPage::Geometry);
        };
        ms.editBurst=[this](std::size_t slot) {
            bursts->editor().selectBurstSlot(slot);selectPage(ShellPage::Bursts);
        };
        main=new MainPageView(state,callbacks,std::move(ms));pages[0].view=main;
        ShellServices ss;ss.tools=tools(-1);
        ss.tabFont=paint.font(9.5,FontWeight::Medium,false);
        ss.placeholderFont=paint.font(11,FontWeight::Medium,false);
        ss.statusFont=metrics(suiteFamily,8.5);ss.bpmFont=metrics(suiteFamily,10);
        ss.selectPage=[this](ShellPage p,bool twice) {
            PostMessageW(root,kSelectPage,WPARAM(p),0);
            if(twice)PostMessageW(root,kDetachPage,WPARAM(p),0);
        };
        ss.toggleDetach=[this](ShellPage p){PostMessageW(root,kDetachPage,WPARAM(p),0);};
        shellView=new ShellView(shell,std::move(ss));shellView->resize(1320,860);
        shellHost=std::make_unique<f::EditorHost>(1320,860,1320,860);
        if(!shellHost->ready())return;
        // Keep an explicit owner reference in addition to the one consumed by
        // the frame. Models and callbacks must survive partial frame setup.
        shellView->remember();
        if(!shellHost->attach(shellView)||!shellHost->setParent(root))return;
        for(unsigned i=0;i<pages.size();++i) {
            auto& p=pages[i];p.context={this,int(i),false};p.floatingContext={this,int(i),true};
            const auto title=w::wide(std::string("s3g Tracker page ")+ShellController::title(ShellPage(i)));
            p.window=CreateWindowExW(0,kWindowClass,title.c_str(),WS_CHILD|WS_CLIPCHILDREN,
                0,40,1320,820,root,nullptr,moduleInstance(),&p.context);
            if(!p.window)return;
            p.host=std::make_unique<f::EditorHost>(1320,820,1320,820);
            if(!p.host->ready())return;
            resizePage(i,1320,820);
            p.view->remember();
            if(!p.host->attach(p.view)||!p.host->setParent(p.window))return;
            p.view->getFrame()->registerKeyboardHook(this);
        }
        shellView->getFrame()->registerKeyboardHook(this);
        registerKeyboard();
        ready=true;selectPage(ShellPage::Tracker);refresh();
    }
    ToolPageServices tools(int page) {
        ToolPageServices result;
        result.font=[this](double size){return metrics(suiteFamily,size);};
        result.color=literalColor;result.fontFactory=fontFactory;
        result.requestNativeFocus=[this,page]{focusPage(page);};
        return result;
    }
    void resizePage(unsigned i,double width,double height) {
        if(auto* p=dynamic_cast<ToolPageView*>(pages[i].view))p->resize(width,height);
        else if(auto* p=dynamic_cast<GeometryPageView*>(pages[i].view))p->resize(width,height);
        else if(auto* p=dynamic_cast<WarpPageView*>(pages[i].view))p->resize(width,height);
    }
    CFrame* activeFrame() {
        const auto focus=GetFocus();
        for(auto& p:pages)if(p.view&&p.window&&(p.window==focus||IsChild(p.window,focus)))
            return p.view->getFrame();
        auto* p=pages[unsigned(shell.selected())].view;
        return p?p->getFrame():nullptr;
    }
    HWND activeWindow() {
        const auto i=unsigned(shell.selected());
        return pages[i].floating?pages[i].floating:root;
    }
    void focusPage(int i) {
        auto* view=i<0?static_cast<f::ContentView*>(shellView):pages[unsigned(i)].view;
        if(!view||!view->getFrame()||!visible)return;
        auto* frame=view->getFrame();
        const auto native=frame->getPlatformFrame();
        if(native)SetFocus(static_cast<HWND>(native->getPlatformRepresentation()));
        static_cast<IPlatformFrameCallback*>(frame)->platformOnActivate(true);
    }
    void refresh() {
        if(!ready||closing||refreshing)return;
        refreshing=true;
        main->reloadModel();geometry->reloadModel();bursts->reloadModel();
        for(auto* p:authoring)p->reloadModel();
        warp->reloadModel();song->modelChanged();console->refresh();
        refreshing=false;
    }
    void refreshPlayback() {
        if(!ready||closing)return;
        auto& state=coordinator->state();
        shell.setHostBpm(state.hostBpm);
        shell.setEventText(state.lastEvent+"  •  SEND "+std::to_string(state.sentEventCount)
            +"  DROP "+std::to_string(state.droppedEventCount)+"  LATE "
            +std::to_string(state.audioLateEventCount)+"  CLK "+std::to_string(state.audioClockFaultCount));
        shellView->invalid();main->refreshPlaybackDisplay();
        geometry->refreshPlaybackDisplay();bursts->refreshPlaybackDisplay();
        for(auto* p:authoring)p->refreshPlaybackDisplay();
        warp->refreshPlaybackDisplay(pages[7].floating||shell.selected()==ShellPage::Warps);
        song->playbackChanged();console->refresh();
    }
    void selectPage(ShellPage page) {
        if(!ShellController::valid(page)||!ready)return;
        shell.select(page);
        for(unsigned i=0;i<pages.size();++i) {
            auto& p=pages[i];
            const bool show=visible&&(p.floating||i==unsigned(page));
            ShowWindow(p.window,show?SW_SHOWNA:SW_HIDE);
            p.host->setVisible(show);
        }
        if(auto floating=pages[unsigned(page)].floating) {
            if(visible){ShowWindow(floating,SW_SHOWNORMAL);SetForegroundWindow(floating);}
        }
        shellView->invalid();
        focusPage(int(page));
        if(page==ShellPage::Tracker)main->focusTracker();
    }
    void layout() {
        if(!ready)return;
        const auto nav=uint32_t(std::lround(40.*height/860.));
        shellHost->setSize(width,height);
        for(auto& p:pages)if(!p.floating) {
            SetWindowPos(p.window,nullptr,0,nav,width,height-nav,SWP_NOZORDER|SWP_NOACTIVATE);
            p.host->setSize(width,height-nav);
        }
    }
    void floatingLayout(unsigned i) {
        auto& p=pages[i];if(!p.floating)return;
        RECT r{};GetClientRect(p.floating,&r);
        if(r.right<=0||r.bottom<=0)return;
        // Detached tools keep their native responsive layout; main plugin
        // resizing alone applies the proportional 65–200% canvas transform.
        SetWindowPos(p.window,nullptr,0,0,r.right,r.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
        resizePage(i,r.right,r.bottom);
        p.host->setSize(r.right,r.bottom);
        p.view->getFrame()->setTransform(CGraphicsTransform());
    }
    void toggleDetach(ShellPage page) {
        if(!ready||!ShellController::canDetach(page))return;
        auto& p=pages[unsigned(page)];
        if(p.floating){reattach(unsigned(page));return;}
        const auto title=w::wide(ShellController::windowTitle(page));
        HWND owner=GetAncestor(nativeParent?nativeParent:root,GA_ROOT);
        p.floating=CreateWindowExW(WS_EX_TOOLWINDOW,kWindowClass,title.c_str(),
            WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,960,700,
            owner,nullptr,moduleInstance(),&p.floatingContext);
        if(!p.floating)return;
        p.host->setVisible(false);
        SetParent(p.window,p.floating);
        shell.setDetached(page,true);
        floatingLayout(unsigned(page));
        selectPage(page);
    }
    void reattach(unsigned i) {
        auto& p=pages[i];if(!p.floating)return;
        p.host->setVisible(false);
        HWND floating=p.floating;p.floating=nullptr;
        SetParent(p.window,root);
        resizePage(i,1320,820);
        shell.setDetached(ShellPage(i),false);
        DestroyWindow(floating);
        layout();selectPage(shell.selected());
    }
    void onKeyboardEvent(KeyboardEvent& event,CFrame* frame) override {
        if(event.type!=EventType::KeyDown||dynamic_cast<CTextEdit*>(frame->getFocusView()))return;
        if(event.modifiers==Modifiers{ModifierKey::Shift}
            &&(event.character=='<'||event.character=='>')) {
            PostMessageW(root,kSelectPage,WPARAM(shell.adjacent(event.character=='>')),0);
            event.consumed=true;
        }
    }
    Page* focusedPage(HWND target) {
        if(!target||!visible)return nullptr;
        const auto focus=GetFocus();
        for(auto& p:pages) {
            if(!p.window||!p.view||!IsWindowVisible(p.window))continue;
            if(focus!=p.window&&!IsChild(p.window,focus))continue;
            // Ancestor targeting is permitted only for this instance's
            // focused page; no sibling FX, project window or other app.
            if(target==focus||target==p.window||IsChild(p.window,target)
                ||IsChild(target,focus))return &p;
        }
        return nullptr;
    }
    static int translate(MSG* msg,w::Accelerator* context) {
        if(!msg||!context)return 0;
        auto* self=static_cast<Impl*>(context->user);
        return self&&self->focusedPage(msg->hwnd)?-1:0;
    }
    static int hwndInfo(HWND hwnd,INT_PTR type) {
        if(type!=0&&type!=1)return 0;
        for(auto* self:instances())
            if(auto* p=self->focusedPage(hwnd))
                if(dynamic_cast<CTextEdit*>(p->view->getFrame()->getFocusView()))return 1;
        return 0;
    }
    void registerKeyboard() {
        reaper=w::bridge(clapHost);
        if(!reaper||!reaper->reg)return;
        const bool shared=std::any_of(instances().begin(),instances().end(),
            [this](auto* i){return i->reaper->reg==reaper->reg&&i->infoRegistered;});
        infoRegistered=shared||reaper->reg("hwnd_info",reinterpret_cast<void*>(hwndInfo))!=0;
        accelerator={translate,true,this};
        acceleratorRegistered=reaper->reg("<accelerator",&accelerator)!=0;
        if(!acceleratorRegistered)acceleratorRegistered=reaper->reg("accelerator",&accelerator)!=0;
        instances().push_back(this);
    }
    void unregisterKeyboard() {
        if(!reaper||!reaper->reg)return;
        if(acceleratorRegistered)reaper->reg("-accelerator",&accelerator);
        auto& list=instances();list.erase(std::remove(list.begin(),list.end(),this),list.end());
        if(infoRegistered&&std::none_of(list.begin(),list.end(),
            [this](auto* i){return i->reaper->reg==reaper->reg&&i->infoRegistered;}))
            reaper->reg("-hwnd_info",reinterpret_cast<void*>(hwndInfo));
    }
    bool show(bool value) {
        if(!ready)return false;
        visible=value;
        ShowWindow(root,value?SW_SHOWNA:SW_HIDE);
        shellHost->setVisible(value);
        for(auto& p:pages)if(p.floating)ShowWindow(p.floating,value?SW_SHOWNA:SW_HIDE);
        if(value){SetTimer(root,1,16,nullptr);selectPage(shell.selected());}
        else {
            KillTimer(root,1);
            for(auto& p:pages)p.host->setVisible(false);
            coordinator->stop();
        }
        return true;
    }
    ~Impl() {
        closing=true;
        unregisterKeyboard();
        if(root)KillTimer(root,1);
        // Page stop hooks need the coordinator. Destroy the coordinator before
        // SongPage's referenced model, and views before their state owner.
        for(auto& p:pages)if(p.view)p.view->stopRefresh();
        if(coordinator)coordinator->stop();
        for(unsigned i=0;i<pages.size();++i)if(i!=1)destroyPage(i);
        if(shellView&&shellView->getFrame())shellView->getFrame()->unregisterKeyboardHook(this);
        shellHost.reset();
        if(shellView)shellView->forget();
        shellView=nullptr;
        coordinator.reset();
        destroyPage(1);
        if(root)DestroyWindow(root);
        if(registeredClass&&!--windowClassUsers)UnregisterClassW(kWindowClass,moduleInstance());
        if(runtime)f::releaseRuntime();
    }
    void destroyPage(unsigned i) {
        auto& p=pages[i];
        if(p.host) {
            if(p.view&&p.view->getFrame())p.view->getFrame()->unregisterKeyboardHook(this);
            p.host.reset();
        }
        if(p.view)p.view->forget();
        p.view=nullptr;
        if(p.window)DestroyWindow(p.window);
        if(p.floating)DestroyWindow(p.floating);
        p.window=p.floating=nullptr;
    }
    static LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        auto* context=reinterpret_cast<WindowContext*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            context=static_cast<WindowContext*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(context));
        }
        auto* self=context?context->owner:nullptr;
        if(self&&!self->closing) {
            if(msg==WM_TIMER&&context->page<0&&self->coordinator) {
                self->coordinator->pollDisplay();return 0;
            }
            if(msg==kSelectPage) {self->selectPage(ShellPage(wp));return 0;}
            if(msg==kDetachPage) {self->toggleDetach(ShellPage(wp));return 0;}
            if(msg==WM_CLOSE&&context->floating) {
                PostMessageW(self->root,kDetachPage,context->page,0);return 0;
            }
            if(msg==WM_SIZE&&context->floating)self->floatingLayout(unsigned(context->page));
            if(msg==WM_GETMINMAXINFO&&context->floating) {
                auto* info=reinterpret_cast<MINMAXINFO*>(lp);
                info->ptMinTrackSize={480,360};return 0;
            }
            if(msg==WM_DPICHANGED&&context->floating) {
                auto* r=reinterpret_cast<RECT*>(lp);
                SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,
                    SWP_NOZORDER|SWP_NOACTIVATE);return 0;
            }
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
};
WindowsTrackerEditor::WindowsTrackerEditor(midi::Engine& engine,const clap_host_t* host)
    : impl_(std::make_unique<Impl>(engine,host)) {}
WindowsTrackerEditor::~WindowsTrackerEditor()=default;
bool WindowsTrackerEditor::ready() const {return impl_&&impl_->ready;}
bool WindowsTrackerEditor::setParent(void* parent) {
    if(!ready()||!parent)return false;
    auto& i=*impl_;i.nativeParent=static_cast<HWND>(parent);
    SetLastError(0);
    const auto old=SetWindowLongPtrW(i.root,GWL_STYLE,WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS);
    if(!old&&GetLastError())return false;
    SetLastError(0);SetParent(i.root,i.nativeParent);
    if(GetLastError())return false;
    SetWindowPos(i.root,nullptr,0,0,i.width,i.height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    return true;
}
bool WindowsTrackerEditor::setSize(uint32_t width,uint32_t height) {
    if(!ready()||!width||!height)return false;
    auto& i=*impl_;i.width=width;i.height=height;
    SetWindowPos(i.root,nullptr,0,0,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    i.layout();return true;
}
bool WindowsTrackerEditor::setVisible(bool visible) {return ready()&&impl_->show(visible);}
void WindowsTrackerEditor::applyDocument(const ProjectDocument& document) {
    if(!ready())return;
    for(auto& page:impl_->pages)page.view->stopRefresh();
    impl_->coordinator->cancelRuntimePublication();
    impl_->coordinator->applyDocument(document);
    impl_->coordinator->resetHistory(document);
    impl_->selectPage(impl_->shell.selected());
}
ProjectDocument WindowsTrackerEditor::currentDocument() {return impl_->coordinator->currentDocument();}
void WindowsTrackerEditor::flush() {if(ready())impl_->coordinator->flushRuntimePublication();}
void WindowsTrackerEditor::cancelPublication() {if(ready())impl_->coordinator->cancelRuntimePublication();}
} // namespace s3g::tracker
