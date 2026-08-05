#include "s3g_embedded_clap_host.h"

#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cstring>

namespace s3g::standalone {
namespace {

int64_t stateWrite(const clap_ostream_t* stream, const void* source,
    uint64_t byteCount)
{
    if (!stream || !stream->ctx || (!source && byteCount > 0u)) return -1;
    auto* destination = static_cast<std::vector<uint8_t>*>(stream->ctx);
    const auto* bytes = static_cast<const uint8_t*>(source);
    destination->insert(destination->end(), bytes, bytes + byteCount);
    return static_cast<int64_t>(byteCount);
}

struct StateReader {
    const std::vector<uint8_t>* source = nullptr;
    size_t offset = 0u;
};

int64_t stateRead(const clap_istream_t* stream, void* destination,
    uint64_t byteCount)
{
    if (!stream || !stream->ctx || (!destination && byteCount > 0u)) return -1;
    auto* reader = static_cast<StateReader*>(stream->ctx);
    if (!reader->source) return -1;
    const size_t remaining = reader->offset < reader->source->size()
        ? reader->source->size() - reader->offset : 0u;
    const size_t count = std::min<size_t>(remaining,
        static_cast<size_t>(byteCount));
    if (count > 0u) {
        std::memcpy(destination, reader->source->data() + reader->offset,
            count);
        reader->offset += count;
    }
    return static_cast<int64_t>(count);
}

void guiResizeHintsChanged(const clap_host_t*) {}

bool guiRequestResize(const clap_host_t* host, uint32_t width,
    uint32_t height)
{
    auto* self = static_cast<EmbeddedClapPlugin*>(host
        ? host->host_data : nullptr);
    if (!self || width == 0u || height == 0u) return false;
    self->notifyGuiResizeRequested(width, height);
    return true;
}

bool guiRequestShow(const clap_host_t* host)
{
    auto* self = static_cast<EmbeddedClapPlugin*>(host
        ? host->host_data : nullptr);
    if (!self) return false;
    self->notifyGuiShowRequested();
    return true;
}

bool guiRequestHide(const clap_host_t* host)
{
    auto* self = static_cast<EmbeddedClapPlugin*>(host
        ? host->host_data : nullptr);
    if (!self) return false;
    self->notifyGuiHideRequested();
    return true;
}

void guiClosed(const clap_host_t* host, bool wasDestroyed)
{
    auto* self = static_cast<EmbeddedClapPlugin*>(host
        ? host->host_data : nullptr);
    if (!self) return;
    self->notifyGuiClosed(wasDestroyed);
}

const clap_host_gui_t kHostGui {
    guiResizeHintsChanged,
    guiRequestResize,
    guiRequestShow,
    guiRequestHide,
    guiClosed,
};

void paramsRescan(const clap_host_t*, clap_param_rescan_flags) {}
void paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void paramsRequestFlush(const clap_host_t* host)
{
    if (auto* self = static_cast<EmbeddedClapPlugin*>(host
            ? host->host_data : nullptr)) {
        self->notifyProcessRequested();
    }
}

const clap_host_params_t kHostParams {
    paramsRescan,
    paramsClear,
    paramsRequestFlush,
};

void stateMarkDirty(const clap_host_t* host)
{
    if (auto* self = static_cast<EmbeddedClapPlugin*>(host
            ? host->host_data : nullptr)) {
        self->notifyStateDirty();
    }
}

const clap_host_state_t kHostState { stateMarkDirty };

} // namespace

EmbeddedClapEntrySession::~EmbeddedClapEntrySession()
{
    deinitialize();
}

bool EmbeddedClapEntrySession::initialize(
    const clap_plugin_entry_t* entry, const char* pluginPath)
{
    deinitialize();
    if (!entry || !pluginPath || pluginPath[0] == '\0'
        || !clap_version_is_compatible(entry->clap_version)
        || !entry->init) return false;
    entry_ = entry;
    pluginPath_ = pluginPath;
    if (!entry_->init(pluginPath_.c_str())) {
        entry_ = nullptr;
        pluginPath_.clear();
        return false;
    }
    initialized_ = true;
    factory_ = entry_->get_factory
        ? static_cast<const clap_plugin_factory_t*>(
            entry_->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    if (!factory_ || !factory_->create_plugin) {
        deinitialize();
        return false;
    }
    return true;
}

void EmbeddedClapEntrySession::deinitialize()
{
    factory_ = nullptr;
    if (initialized_ && entry_ && entry_->deinit) entry_->deinit();
    entry_ = nullptr;
    pluginPath_.clear();
    initialized_ = false;
}

EmbeddedClapPlugin::EmbeddedClapPlugin()
{
    host_.clap_version = CLAP_VERSION_INIT;
    host_.host_data = this;
    host_.vendor = "s3g";
    host_.url = "https://github.com/s3g/s3g-dsp";
    host_.version = "1";
    host_.get_extension = hostGetExtension;
    host_.request_restart = hostRequestRestart;
    host_.request_process = hostRequestProcess;
    host_.request_callback = hostRequestCallback;
}

EmbeddedClapPlugin::~EmbeddedClapPlugin() { destroy(); }

bool EmbeddedClapPlugin::create(const clap_plugin_entry_t* entry,
    const char* pluginId, const char* hostName)
{
    destroy();
    if (!entry || !pluginId || !hostName
        || !clap_version_is_compatible(entry->clap_version)) return false;
    entry_ = entry;
    hostName_ = hostName;
    pluginPath_ = std::string("embedded://") + pluginId;
    host_.name = hostName_.c_str();
    if (!entry_->init || !entry_->init(pluginPath_.c_str())) {
        entry_ = nullptr;
        return false;
    }
    entryInitialized_ = true;
    const auto* factory = entry_->get_factory
        ? static_cast<const clap_plugin_factory_t*>(
            entry_->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    plugin_ = factory && factory->create_plugin
        ? factory->create_plugin(factory, &host_, pluginId) : nullptr;
    if (!plugin_ || !plugin_->init || !plugin_->init(plugin_)) {
        destroy();
        return false;
    }
    return true;
}

bool EmbeddedClapPlugin::create(
    const EmbeddedClapEntrySession& entrySession,
    const char* pluginId, const char* hostName)
{
    destroy();
    if (!entrySession.isInitialized() || !entrySession.entry()
        || !entrySession.factory() || !pluginId || !hostName) return false;
    entry_ = entrySession.entry();
    hostName_ = hostName;
    host_.name = hostName_.c_str();
    plugin_ = entrySession.factory()->create_plugin(
        entrySession.factory(), &host_, pluginId);
    if (!plugin_ || !plugin_->init || !plugin_->init(plugin_)) {
        destroy();
        return false;
    }
    return true;
}

void EmbeddedClapPlugin::destroy()
{
    deactivate();
    if (plugin_ && plugin_->destroy) plugin_->destroy(plugin_);
    plugin_ = nullptr;
    if (entryInitialized_ && entry_ && entry_->deinit) entry_->deinit();
    entry_ = nullptr;
    entryInitialized_ = false;
    hostName_.clear();
    pluginPath_.clear();
}

bool EmbeddedClapPlugin::activate(double sampleRate, uint32_t minFrames,
    uint32_t maxFrames)
{
    if (!plugin_ || active_ || sampleRate <= 0.0 || minFrames == 0u
        || maxFrames < minFrames || !plugin_->activate) return false;
    if (!plugin_->activate(plugin_, sampleRate, minFrames, maxFrames))
        return false;
    active_ = true;
    if (plugin_->start_processing) {
        processing_ = plugin_->start_processing(plugin_);
        if (!processing_) {
            plugin_->deactivate(plugin_);
            active_ = false;
            return false;
        }
    }
    return true;
}

void EmbeddedClapPlugin::deactivate()
{
    if (!plugin_ || !active_) return;
    if (processing_ && plugin_->stop_processing)
        plugin_->stop_processing(plugin_);
    processing_ = false;
    if (plugin_->deactivate) plugin_->deactivate(plugin_);
    active_ = false;
}

void EmbeddedClapPlugin::reset()
{
    if (plugin_ && plugin_->reset) plugin_->reset(plugin_);
}

clap_process_status EmbeddedClapPlugin::process(
    const clap_process_t& processBlock) const
{
    return plugin_ && plugin_->process
        ? plugin_->process(plugin_, &processBlock) : CLAP_PROCESS_ERROR;
}

bool EmbeddedClapPlugin::saveState(std::vector<uint8_t>& destination) const
{
    const auto* state = extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    if (!state || !state->save) return false;
    destination.clear();
    clap_ostream_t stream { &destination, stateWrite };
    if (!state->save(plugin_, &stream)) {
        destination.clear();
        return false;
    }
    return true;
}

bool EmbeddedClapPlugin::loadState(const std::vector<uint8_t>& source)
{
    const auto* state = extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    if (!state || !state->load || source.empty()) return false;
    StateReader reader { &source, 0u };
    clap_istream_t stream { &reader, stateRead };
    return state->load(plugin_, &stream) && reader.offset == source.size();
}

bool EmbeddedClapPlugin::takeRestartRequest()
{
    return restartRequested_.exchange(false, std::memory_order_acq_rel);
}

bool EmbeddedClapPlugin::takeProcessRequest()
{
    return processRequested_.exchange(false, std::memory_order_acq_rel);
}

bool EmbeddedClapPlugin::takeCallbackRequest()
{
    return callbackRequested_.exchange(false, std::memory_order_acq_rel);
}

bool EmbeddedClapPlugin::takeStateDirty()
{
    return stateDirty_.exchange(false, std::memory_order_acq_rel);
}

bool EmbeddedClapPlugin::takeGuiResizeRequest(uint32_t& width,
    uint32_t& height)
{
    const uint64_t packed = guiResizeRequest_.exchange(0u,
        std::memory_order_acq_rel);
    if (packed == 0u) return false;
    width = static_cast<uint32_t>(packed >> 32u);
    height = static_cast<uint32_t>(packed & 0xffffffffu);
    return width > 0u && height > 0u;
}

bool EmbeddedClapPlugin::takeGuiShowRequest()
{
    return guiShowRequested_.exchange(false, std::memory_order_acq_rel);
}

bool EmbeddedClapPlugin::takeGuiHideRequest()
{
    return guiHideRequested_.exchange(false, std::memory_order_acq_rel);
}

bool EmbeddedClapPlugin::takeGuiClosed(bool& wasDestroyed)
{
    const uint32_t state = guiClosedState_.exchange(0u,
        std::memory_order_acq_rel);
    wasDestroyed = state == 2u;
    return state != 0u;
}

void EmbeddedClapPlugin::serviceMainThreadCallback()
{
    if (takeCallbackRequest() && plugin_ && plugin_->on_main_thread)
        plugin_->on_main_thread(plugin_);
}

void EmbeddedClapPlugin::notifyProcessRequested()
{
    processRequested_.store(true, std::memory_order_release);
}

void EmbeddedClapPlugin::notifyStateDirty()
{
    stateDirty_.store(true, std::memory_order_release);
}

void EmbeddedClapPlugin::notifyGuiResizeRequested(uint32_t width,
    uint32_t height)
{
    const uint64_t packed = (static_cast<uint64_t>(width) << 32u)
        | static_cast<uint64_t>(height);
    guiResizeRequest_.store(packed, std::memory_order_release);
}

void EmbeddedClapPlugin::notifyGuiShowRequested()
{
    guiShowRequested_.store(true, std::memory_order_release);
}

void EmbeddedClapPlugin::notifyGuiHideRequested()
{
    guiHideRequested_.store(true, std::memory_order_release);
}

void EmbeddedClapPlugin::notifyGuiClosed(bool wasDestroyed)
{
    guiClosedState_.store(wasDestroyed ? 2u : 1u,
        std::memory_order_release);
}

EmbeddedClapPlugin* EmbeddedClapPlugin::fromHost(const clap_host_t* host)
{
    return static_cast<EmbeddedClapPlugin*>(host ? host->host_data : nullptr);
}

const void* EmbeddedClapPlugin::hostGetExtension(const clap_host_t*,
    const char* extensionId)
{
    if (!extensionId) return nullptr;
    if (std::strcmp(extensionId, CLAP_EXT_GUI) == 0) return &kHostGui;
    if (std::strcmp(extensionId, CLAP_EXT_PARAMS) == 0) return &kHostParams;
    if (std::strcmp(extensionId, CLAP_EXT_STATE) == 0) return &kHostState;
    return nullptr;
}

void EmbeddedClapPlugin::hostRequestRestart(const clap_host_t* host)
{
    if (auto* self = fromHost(host))
        self->restartRequested_.store(true, std::memory_order_release);
}

void EmbeddedClapPlugin::hostRequestProcess(const clap_host_t* host)
{
    if (auto* self = fromHost(host))
        self->processRequested_.store(true, std::memory_order_release);
}

void EmbeddedClapPlugin::hostRequestCallback(const clap_host_t* host)
{
    if (auto* self = fromHost(host))
        self->callbackRequested_.store(true, std::memory_order_release);
}

} // namespace s3g::standalone
