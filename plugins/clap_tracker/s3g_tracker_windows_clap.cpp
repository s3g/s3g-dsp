#include "s3g_tracker_windows_editor.h"
#include "s3g_tracker_clap_adapter.h"
#include "s3g/tracker/project_codec.h"
#include "s3g_clap_vstgui.h"
#include <cstring>
#include <new>
namespace {
using namespace s3g::tracker;
constexpr uint32_t kWidth=1320, kHeight=860;
struct Plugin : midi::Engine {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    std::unique_ptr<WindowsTrackerEditor> editor;
    uint32_t width=kWidth, height=kHeight;
};
Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* p) { delete self(p); }
bool activate(const clap_plugin_t* p, double rate, uint32_t, uint32_t)
{ return midi::activate(*self(p), rate); }
void deactivate(const clap_plugin_t* p) { midi::deactivate(*self(p)); }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* p) { midi::reset(*self(p)); }
clap_process_status process(const clap_plugin_t* p, const clap_process_t* data)
{ return clap_adapter::process(*self(p), data); }
void onMainThread(const clap_plugin_t* p) { midi::drainRetiredRuntimes(*self(p)); }
uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    (void)isInput;
    return 1u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 50u : 100u;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "MIDI Record Input" : "Tracker MIDI Output");
    return true;
}

const clap_plugin_note_ports_t notePorts {notePortsCount, notePortsGet};
bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* instance = self(plugin);
    ProjectDocument document;
    if (instance->editor)
        document = instance->editor->currentDocument();
    else {
        std::lock_guard<std::mutex> lock(instance->documentMutex);
        document = instance->document;
    }
    std::string json;
    const auto result = s3g::tracker::encodeProjectDocument(document, json);
    if (!result.ok()) return false;
    std::size_t written = 0u;
    while (written < json.size()) {
        const int64_t amount = stream->write(stream,
            json.data() + written, json.size() - written);
        if (amount <= 0 || static_cast<uint64_t>(amount)
                > json.size() - written) return false;
        written += static_cast<std::size_t>(amount);
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    std::string json;
    std::array<char, 8192u> buffer {};
    for (;;) {
        const int64_t amount = stream->read(stream,
            buffer.data(), buffer.size());
        if (amount < 0 || static_cast<uint64_t>(amount) > buffer.size())
            return false;
        if (amount == 0) break;
        if (json.size() + static_cast<std::size_t>(amount)
            > s3g::tracker::kMaximumProjectDocumentBytes) return false;
        json.append(buffer.data(), static_cast<std::size_t>(amount));
    }
    ProjectDocument document;
    const auto result = s3g::tracker::decodeProjectDocument(json, document);
    if (!result.ok()) return false;
    auto* instance = self(plugin);
    if (instance->editor)
        instance->editor->cancelPublication();
    publishDocument(*instance, document, false);
    if (instance->editor) {
        instance->editor->applyDocument(document);
    }
    return true;
}

const clap_plugin_state_t stateExtension {stateSave,stateLoad};

bool guiIsApiSupported(const clap_plugin_t*,const char* api,bool floating)
{ return !floating && api && !std::strcmp(api,CLAP_WINDOW_API_WIN32); }
bool guiGetPreferredApi(const clap_plugin_t*,const char** api,bool* floating)
{ if(!api||!floating)return false;*api=CLAP_WINDOW_API_WIN32;*floating=false;return true; }
bool guiCreate(const clap_plugin_t* p,const char* api,bool floating)
{
    if(!guiIsApiSupported(p,api,floating))return false;
    auto& instance=*self(p);
    if(instance.editor)return true;
    auto editor=std::make_unique<WindowsTrackerEditor>(instance,instance.host);
    if(!editor->ready())return false;
    editor->setSize(instance.width,instance.height);
    instance.editor=std::move(editor);
    return true;
}
void guiDestroy(const clap_plugin_t* p) { self(p)->editor.reset(); }
bool guiSetScale(const clap_plugin_t*,double) { return false; }
bool guiGetSize(const clap_plugin_t* p,uint32_t* w,uint32_t* h)
{ if(!w||!h)return false;*w=self(p)->width;*h=self(p)->height;return true; }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*,clap_gui_resize_hints_t* h)
{ return s3g::clap_gui::portable::getResizeHints(kWidth,kHeight,h); }
bool guiAdjustSize(const clap_plugin_t*,uint32_t* w,uint32_t* h)
{
    if(!w||!h)return false;
    const double sx=double(*w)/kWidth,sy=double(*h)/kHeight;
    if(sx>=.65&&sx<=2&&sy>=.65&&sy<=2&&std::abs(sx-sy)<=.5/kWidth+.5/kHeight)return true;
    return s3g::clap_gui::portable::adjustSize(kWidth,kHeight,w,h);
}
bool guiSetSize(const clap_plugin_t* p,uint32_t w,uint32_t h)
{
    if(!guiAdjustSize(p,&w,&h))return false;
    auto& i=*self(p);
    if(i.editor&&!i.editor->setSize(w,h))return false;
    i.width=w;i.height=h;return true;
}
bool guiSetParent(const clap_plugin_t* p,const clap_window_t* w)
{ return w&&w->api&&!std::strcmp(w->api,CLAP_WINDOW_API_WIN32)&&self(p)->editor&&self(p)->editor->setParent(w->win32); }
bool guiSetTransient(const clap_plugin_t*,const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*,const char*) {}
bool guiShow(const clap_plugin_t* p) { return self(p)->editor&&self(p)->editor->setVisible(true); }
bool guiHide(const clap_plugin_t* p) { return self(p)->editor&&self(p)->editor->setVisible(false); }
const clap_plugin_gui_t guiExtension {
 guiIsApiSupported,guiGetPreferredApi,guiCreate,guiDestroy,guiSetScale,guiGetSize,
 guiCanResize,guiGetResizeHints,guiAdjustSize,guiSetSize,guiSetParent,guiSetTransient,guiSuggestTitle,guiShow,guiHide};
const void* pluginGetExtension(const clap_plugin_t*,const char* id)
{
 if(!id)return nullptr;
 if(!std::strcmp(id,CLAP_EXT_GUI))return &guiExtension;
 if(!std::strcmp(id,CLAP_EXT_STATE))return &stateExtension;
 if(!std::strcmp(id,CLAP_EXT_NOTE_PORTS))return &notePorts;
 return nullptr;
}
const char* const features[] {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.tracker",
    "s3g Tracker",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0",
    "Polymetric tracker, song sequencer, and sample-accurate MIDI generator.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->services = s3g::tracker::clap_adapter::hostServices(host);
    if (!s3g::tracker::midi::initialize(*instance)) {
        delete instance;
        return nullptr;
    }
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{ return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory {factoryGetPluginCount,factoryGetPluginDescriptor,createPlugin};
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id)
{ return id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr; }
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
 CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
