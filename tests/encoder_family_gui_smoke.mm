#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>

namespace {

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

bool closeEnough(CGFloat a, CGFloat b)
{
    return std::fabs(a - b) < 0.5;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: s3g_encoder_family_gui_smoke <plugin binary> <plugin id> <native width> <native height>\n";
        return 2;
    }

    const char* pluginId = argv[2];
    const uint32_t nativeWidth = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
    const uint32_t nativeHeight = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
    if (nativeWidth < 480u || nativeHeight < 360u) return 2;

    @autoreleasepool {
        void* library = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
        if (!library) {
            std::cerr << "Could not load encoder: " << dlerror() << "\n";
            return 1;
        }
        const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
        if (!entry || !entry->init(argv[1])) {
            std::cerr << "Could not initialize encoder CLAP entry\n";
            return 1;
        }

        clap_host_t host {};
        host.clap_version = CLAP_VERSION_INIT;
        host.name = "s3g encoder-family GUI smoke";
        host.vendor = "s3g";
        host.url = "https://github.com/s3g/s3g-dsp";
        host.version = "1";
        host.get_extension = hostGetExtension;
        host.request_restart = hostRequest;
        host.request_process = hostRequest;
        host.request_callback = hostRequest;

        const auto* factory = static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        const clap_plugin_t* plugin = factory
            ? factory->create_plugin(factory, &host, pluginId)
            : nullptr;
        if (!plugin || !plugin->init(plugin)) {
            std::cerr << "Could not create encoder " << pluginId << "\n";
            return 1;
        }

        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            plugin->get_extension(plugin, CLAP_EXT_GUI));
        bool ok = gui
            && gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false)
            && gui->can_resize(plugin);
        clap_gui_resize_hints_t hints {};
        ok = ok && gui->get_resize_hints(plugin, &hints)
            && hints.can_resize_horizontally && hints.can_resize_vertically
            && !hints.preserve_aspect_ratio;

        uint32_t width = 0u;
        uint32_t height = 0u;
        ok = ok && gui->get_size(plugin, &width, &height)
            && width >= 480u && width <= nativeWidth
            && height >= 360u && height <= nativeHeight;
        uint32_t minimumWidth = 1u;
        uint32_t minimumHeight = 1u;
        ok = ok && gui->adjust_size(plugin, &minimumWidth, &minimumHeight)
            && minimumWidth == 480u && minimumHeight == 360u;
        ok = ok && gui->create(plugin, CLAP_WINDOW_API_COCOA, false);

        const uint32_t testWidth = std::min(720u, nativeWidth);
        const uint32_t testHeight = std::min(540u, nativeHeight);
        ok = ok && gui->set_size(plugin, testWidth, testHeight)
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
            && closeEnough([document frame].size.width, nativeWidth)
            && closeEnough([document frame].size.height, nativeHeight);
        if (ok) {
            [[scroll contentView] scrollToPoint:NSMakePoint(120.0, 70.0)];
            [scroll reflectScrolledClipView:[scroll contentView]];
            const NSPoint origin = [[scroll contentView] bounds].origin;
            ok = origin.x > 100.0 && origin.y > 50.0;
        }
        ok = ok && gui->show(plugin) && gui->hide(plugin);

        gui->destroy(plugin);
        [parent release];
        plugin->destroy(plugin);

        // Also exercise a host tearing down the plug-in while its GUI still
        // exists. Every family member owns the viewport through plug-in
        // destruction as a defensive fallback.
        const clap_plugin_t* teardownPlugin = factory
            ? factory->create_plugin(factory, &host, pluginId)
            : nullptr;
        if (teardownPlugin && teardownPlugin->init(teardownPlugin)) {
            const auto* teardownGui = static_cast<const clap_plugin_gui_t*>(
                teardownPlugin->get_extension(teardownPlugin, CLAP_EXT_GUI));
            NSView* teardownParent = [[NSView alloc] initWithFrame:NSMakeRect(
                0.0, 0.0, testWidth, testHeight)];
            clap_window_t teardownWindow {};
            teardownWindow.api = CLAP_WINDOW_API_COCOA;
            teardownWindow.cocoa = teardownParent;
            ok = ok && teardownGui
                && teardownGui->create(teardownPlugin, CLAP_WINDOW_API_COCOA, false)
                && teardownGui->set_size(teardownPlugin, testWidth, testHeight)
                && teardownGui->set_parent(teardownPlugin, &teardownWindow)
                && teardownGui->show(teardownPlugin);
            teardownPlugin->destroy(teardownPlugin);
            ok = ok && [[teardownParent subviews] count] == 0u;
            [teardownParent release];
        } else {
            ok = false;
        }
        entry->deinit();
        // Keep the bundle loaded: Objective-C classes remain registered for the
        // process lifetime, which mirrors production plug-in hosts.

        if (!ok) {
            std::cerr << "Responsive GUI smoke failed for " << pluginId << "\n";
            return 1;
        }
        std::cout << "Responsive GUI smoke passed for " << pluginId << "\n";
    }
    return 0;
}
