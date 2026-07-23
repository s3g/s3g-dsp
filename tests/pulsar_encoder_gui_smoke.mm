#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "../dsp/s3g_ambi_pulsar_encoder.h"

#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <vector>

namespace {

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

bool closeEnough(CGFloat a, CGFloat b)
{
    return std::fabs(a - b) < 0.5;
}

struct MemoryState {
    std::vector<uint8_t> bytes {};
    size_t readOffset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream, const void* buffer, uint64_t size)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    state->bytes.insert(state->bytes.end(), bytes, bytes + size);
    return static_cast<int64_t>(size);
}

int64_t stateRead(const clap_istream_t* stream, void* buffer, uint64_t size)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const size_t available = state->bytes.size() - std::min(state->readOffset, state->bytes.size());
    const size_t count = std::min<size_t>(static_cast<size_t>(size), available);
    if (count == 0u) return 0;
    std::memcpy(buffer, state->bytes.data() + state->readOffset, count);
    state->readOffset += count;
    return static_cast<int64_t>(count);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_ambi_pulsar_encoder_gui_smoke <plugin binary>\n";
        return 2;
    }

    @autoreleasepool {
        void* library = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
        if (!library) {
            std::cerr << "Could not load Pulsar Encoder: " << dlerror() << "\n";
            return 1;
        }
        const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
        if (!entry || !entry->init(argv[1])) {
            std::cerr << "Could not initialize Pulsar Encoder CLAP entry\n";
            dlclose(library);
            return 1;
        }

        clap_host_t host {};
        host.clap_version = CLAP_VERSION_INIT;
        host.name = "s3g Pulsar GUI smoke";
        host.vendor = "s3g";
        host.url = "https://github.com/s3g/s3g-dsp";
        host.version = "1";
        host.get_extension = hostGetExtension;
        host.request_restart = hostRequest;
        host.request_process = hostRequest;
        host.request_callback = hostRequest;

        const auto* factory = static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        const clap_plugin_t* plugin = factory ? factory->create_plugin(factory, &host,
            "org.s3g.s3g-dsp.ambi-pulsar-encoder-64") : nullptr;
        if (!plugin || !plugin->init(plugin)) {
            std::cerr << "Could not create Pulsar Encoder\n";
            entry->deinit();
            dlclose(library);
            return 1;
        }

        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            plugin->get_extension(plugin, CLAP_EXT_GUI));
        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        const auto* state = static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE));
        bool ok = gui
            && gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false)
            && gui->can_resize(plugin)
            && params && state;
        uint32_t listeningParams = 0u;
        if (params) {
            const uint32_t count = params->count(plugin);
            for (uint32_t index = 0u; index < count; ++index) {
                clap_param_info_t info {};
                if (params->get_info(plugin, index, &info)
                    && info.id >= 100u && info.id <= 108u
                    && std::strcmp(info.module, "Field Listening") == 0) {
                    ++listeningParams;
                }
            }
        }
        double enabled = -1.0;
        double bypass = -1.0;
        ok = ok && listeningParams == 9u
            && params->get_value(plugin, 101u, &enabled)
            && params->get_value(plugin, 102u, &bypass)
            && enabled == 0.0 && bypass == 0.0;
        MemoryState savedState;
        clap_ostream_t outputState { &savedState, stateWrite };
        ok = ok && state->save(plugin, &outputState) && savedState.bytes.size() > 100u;
        clap_istream_t inputState { &savedState, stateRead };
        ok = ok && state->load(plugin, &inputState)
            && savedState.readOffset == savedState.bytes.size();
        // Version 8 appended a 36-byte listening suffix to the parameter
        // struct. Reconstruct the immediately preceding version-7 stream from
        // the saved state and verify that its legacy prefix still upgrades.
        constexpr size_t kStateHeaderSize = sizeof(uint32_t);
        constexpr size_t kStateTailSize = sizeof(uint32_t) + sizeof(int32_t)
            + sizeof(float) + 64u;
        constexpr size_t kListeningSuffixSize = sizeof(s3g::AmbiPulsarListeningParams);
        if (ok && savedState.bytes.size() > kStateHeaderSize + kStateTailSize + kListeningSuffixSize) {
            const size_t paramsSize = savedState.bytes.size() - kStateHeaderSize - kStateTailSize;
            MemoryState legacyState;
            const uint32_t version7 = 7u;
            const auto* versionBytes = reinterpret_cast<const uint8_t*>(&version7);
            legacyState.bytes.insert(legacyState.bytes.end(), versionBytes,
                versionBytes + sizeof(version7));
            legacyState.bytes.insert(legacyState.bytes.end(),
                savedState.bytes.begin() + kStateHeaderSize,
                savedState.bytes.begin() + kStateHeaderSize + paramsSize - kListeningSuffixSize);
            legacyState.bytes.insert(legacyState.bytes.end(),
                savedState.bytes.begin() + kStateHeaderSize + paramsSize,
                savedState.bytes.end());
            clap_istream_t legacyInput { &legacyState, stateRead };
            ok = state->load(plugin, &legacyInput)
                && legacyState.readOffset == legacyState.bytes.size()
                && params->get_value(plugin, 101u, &enabled)
                && params->get_value(plugin, 102u, &bypass)
                && enabled == 0.0 && bypass == 0.0;
        } else {
            ok = false;
        }
        clap_gui_resize_hints_t hints {};
        ok = ok && gui->get_resize_hints(plugin, &hints)
            && hints.can_resize_horizontally && hints.can_resize_vertically
            && !hints.preserve_aspect_ratio;

        uint32_t width = 0u;
        uint32_t height = 0u;
        ok = ok && gui->get_size(plugin, &width, &height)
            && width >= 480u && width <= 1160u
            && height >= 360u && height <= 858u;
        uint32_t minimumWidth = 1u;
        uint32_t minimumHeight = 1u;
        ok = ok && gui->adjust_size(plugin, &minimumWidth, &minimumHeight)
            && minimumWidth == 480u && minimumHeight == 360u;
        ok = ok && gui->create(plugin, CLAP_WINDOW_API_COCOA, false);

        uint32_t testWidth = 720u;
        uint32_t testHeight = 540u;
        ok = ok && gui->adjust_size(plugin, &testWidth, &testHeight)
            && testWidth == 720u && testHeight == 540u
            && gui->set_size(plugin, testWidth, testHeight)
            && gui->get_size(plugin, &width, &height)
            && width == testWidth && height == testHeight;

        NSView* parent = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, testWidth, testHeight)];
        clap_window_t window {};
        window.api = CLAP_WINDOW_API_COCOA;
        window.cocoa = parent;
        ok = ok && gui->set_parent(plugin, &window) && [[parent subviews] count] == 1u;
        NSView* root = ok ? [[parent subviews] objectAtIndex:0u] : nil;
        ok = ok && [root isKindOfClass:[NSScrollView class]];
        NSScrollView* scroll = ok ? static_cast<NSScrollView*>(root) : nil;
        NSView* document = scroll ? [scroll documentView] : nil;
        ok = ok && [scroll hasHorizontalScroller] && [scroll hasVerticalScroller]
            && document
            && closeEnough([document frame].size.width, 1160.0)
            && closeEnough([document frame].size.height, 858.0);
        if (ok) {
            [[scroll contentView] scrollToPoint:NSMakePoint(120.0, 140.0)];
            [scroll reflectScrolledClipView:[scroll contentView]];
            const NSPoint origin = [[scroll contentView] bounds].origin;
            ok = origin.x > 100.0 && origin.y > 120.0;
        }
        ok = ok && gui->show(plugin) && gui->hide(plugin);

        gui->destroy(plugin);
        [parent release];
        plugin->destroy(plugin);
        entry->deinit();
        // Objective-C classes registered by a plug-in remain process-global;
        // production hosts likewise keep the bundle loaded after GUI teardown.

        if (!ok) {
            std::cerr << "Pulsar Encoder responsive GUI smoke test failed\n";
            return 1;
        }
        std::cout << "s3g Pulsar Encoder responsive GUI smoke test passed\n";
    }
    return 0;
}
