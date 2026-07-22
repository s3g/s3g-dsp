#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include <cmath>
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
