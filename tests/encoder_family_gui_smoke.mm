#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>

#include "../plugins/common/s3g_cocoa_gui.h"

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
    if (argc != 5 && argc != 7) {
        std::cerr
            << "usage: s3g_encoder_family_gui_smoke <plugin binary> <plugin id>"
            << " <native width> <native height> [host-name prefix responsive|fixed]\n";
        return 2;
    }

    const char* pluginId = argv[2];
    const uint32_t nativeWidth = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
    const uint32_t nativeHeight = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
    const char* expectedNamePrefix =
        argc == 7 ? argv[5] : "s3g Ambi Encoder ";
    const bool responsive = argc != 7 || std::strcmp(argv[6], "responsive") == 0;
    const bool fixed = argc == 7 && std::strcmp(argv[6], "fixed") == 0;
    if ((!responsive && !fixed)
        || nativeWidth < 480u
        || nativeHeight < (responsive ? 360u : 240u)) {
        return 2;
    }

    @autoreleasepool {
        NSString* fittedValue = s3g::clap_gui::sliderValueTextToFit(
            @"12.34567 dB", 36.0, s3g::clap_gui::softValueAttrs());
        if ([fittedValue sizeWithAttributes:s3g::clap_gui::softValueAttrs()].width > 36.0) {
            std::cerr << "Shared slider value fitting exceeded its value cell\n";
            return 1;
        }
        NSDictionary* menuAttrs = s3g::clap_gui::softValueAttrs();
        NSString* uppercaseMenu = s3g::clap_gui::menuDisplayText(
            @"Virtual field", 180.0, menuAttrs);
        NSString* compactMenu = s3g::clap_gui::menuDisplayText(
            @"Energy-normalized cardioid", 76.0, menuAttrs);
        if (![uppercaseMenu isEqualToString:@"VIRTUAL FIELD"]
            || ![compactMenu isEqualToString:[compactMenu uppercaseString]]
            || [compactMenu sizeWithAttributes:menuAttrs].width > 76.0) {
            std::cerr << "Shared menu capitalization or fitting contract failed\n";
            return 1;
        }

        void* library = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
        if (!library) {
            std::cerr << "Could not load family member: " << dlerror() << "\n";
            return 1;
        }
        const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
        if (!entry || !entry->init(argv[1])) {
            std::cerr << "Could not initialize family CLAP entry\n";
            return 1;
        }

        clap_host_t host {};
        host.clap_version = CLAP_VERSION_INIT;
        host.name = "s3g family GUI smoke";
        host.vendor = "s3g";
        host.url = "https://github.com/s3g/s3g-dsp";
        host.version = "1";
        host.get_extension = hostGetExtension;
        host.request_restart = hostRequest;
        host.request_process = hostRequest;
        host.request_callback = hostRequest;

        const auto* factory = static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        const clap_plugin_descriptor_t* requestedDescriptor = nullptr;
        if (factory) {
            const uint32_t descriptorCount =
                factory->get_plugin_count(factory);
            for (uint32_t index = 0u; index < descriptorCount; ++index) {
                const auto* candidate =
                    factory->get_plugin_descriptor(factory, index);
                if (candidate && candidate->id
                    && std::strcmp(candidate->id, pluginId) == 0) {
                    requestedDescriptor = candidate;
                    break;
                }
            }
        }
        if (!requestedDescriptor || !requestedDescriptor->name
            || std::strncmp(requestedDescriptor->name, expectedNamePrefix,
                std::strlen(expectedNamePrefix)) != 0) {
            std::cerr << "Host name is outside the requested family order: "
                << (requestedDescriptor && requestedDescriptor->name
                    ? requestedDescriptor->name : "(missing)") << "\n";
            return 1;
        }
        const clap_plugin_t* plugin = factory
            ? factory->create_plugin(factory, &host, pluginId)
            : nullptr;
        if (!plugin || !plugin->init(plugin)) {
            std::cerr << "Could not create family member " << pluginId << "\n";
            return 1;
        }

        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        clap_param_info_t firstParam {};
        NSEvent* doubleClick = [NSEvent
            mouseEventWithType:NSEventTypeLeftMouseDown
            location:NSZeroPoint
            modifierFlags:0
            timestamp:0.0
            windowNumber:0
            context:nil
            eventNumber:0
            clickCount:2
            pressure:1.0];
        NSEvent* singleClick = [NSEvent
            mouseEventWithType:NSEventTypeLeftMouseDown
            location:NSZeroPoint
            modifierFlags:0
            timestamp:0.0
            windowNumber:0
            context:nil
            eventNumber:0
            clickCount:1
            pressure:1.0];
        double resolvedDefault = 0.0;
        bool ok = params && params->count && params->get_info
            && params->count(plugin) > 0u
            && params->get_info(plugin, 0u, &firstParam)
            && s3g::clap_gui::sliderDoubleClickDefault(
                doubleClick, plugin, firstParam.id, &resolvedDefault)
            && resolvedDefault == firstParam.default_value
            && !s3g::clap_gui::sliderDoubleClickDefault(
                singleClick, plugin, firstParam.id, &resolvedDefault);

        if (ok && std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-wave-terrain-encoder-64") == 0) {
            bool foundScale = false;
            bool foundInterpretation = false;
            const uint32_t parameterCount = params->count(plugin);
            for (uint32_t index = 0u; index < parameterCount; ++index) {
                clap_param_info_t info {};
                if (!params->get_info(plugin, index, &info)) {
                    ok = false;
                    break;
                }
                if (std::strcmp(info.name, "Pitch Scale") == 0) {
                    char text[64] {};
                    double parsed = -1.0;
                    double removed = -1.0;
                    foundScale = info.max_value == 101.0
                        && params->value_to_text(
                            plugin, info.id, info.max_value,
                            text, sizeof(text))
                        && std::strcmp(text, "COMPOSITE BLUES") == 0
                        && params->text_to_value(
                            plugin, info.id, "MINOR PENTATONIC", &parsed)
                        && parsed == 32.0
                        && !params->text_to_value(
                            plugin, info.id, "VECTOR", &removed);
                } else if (std::strcmp(info.name, "Interpretation") == 0) {
                    double removed = -1.0;
                    foundInterpretation = info.max_value == 8.0
                        && !params->text_to_value(
                            plugin, info.id, "VECTOR", &removed);
                }
            }
            ok = ok && foundScale && foundInterpretation;
        }

        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            plugin->get_extension(plugin, CLAP_EXT_GUI));
        ok = ok && gui
            && gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false);

        uint32_t width = 0u;
        uint32_t height = 0u;
        if (responsive) {
            clap_gui_resize_hints_t hints {};
            ok = ok && gui->can_resize(plugin)
                && gui->get_resize_hints(plugin, &hints)
                && hints.can_resize_horizontally && hints.can_resize_vertically
                && !hints.preserve_aspect_ratio
                && gui->get_size(plugin, &width, &height)
                && width >= 480u && width <= nativeWidth
                && height >= 360u && height <= nativeHeight;
            uint32_t minimumWidth = 1u;
            uint32_t minimumHeight = 1u;
            ok = ok && gui->adjust_size(plugin, &minimumWidth, &minimumHeight)
                && minimumWidth == 480u && minimumHeight == 360u;
        } else {
            ok = ok && !gui->can_resize(plugin)
                && gui->get_size(plugin, &width, &height)
                && width == nativeWidth && height == nativeHeight;
        }
        ok = ok && gui->create(plugin, CLAP_WINDOW_API_COCOA, false);

        const uint32_t testWidth =
            responsive ? std::min(720u, nativeWidth) : nativeWidth;
        const uint32_t testHeight =
            responsive ? std::min(540u, nativeHeight) : nativeHeight;
        if (responsive) {
            ok = ok && gui->set_size(plugin, testWidth, testHeight)
                && gui->get_size(plugin, &width, &height)
                && width == testWidth && height == testHeight;
        }

        NSView* parent = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, testWidth, testHeight)];
        clap_window_t window {};
        window.api = CLAP_WINDOW_API_COCOA;
        window.cocoa = parent;
        ok = ok && gui->set_parent(plugin, &window) && [[parent subviews] count] == 1u;
        NSView* root = ok ? [[parent subviews] objectAtIndex:0u] : nil;
        NSScrollView* scroll = nil;
        NSView* document = root;
        if (responsive) {
            ok = ok && [root isKindOfClass:[NSScrollView class]];
            scroll = ok ? static_cast<NSScrollView*>(root) : nil;
            document = scroll ? [scroll documentView] : nil;
            ok = ok && [scroll hasHorizontalScroller] && [scroll hasVerticalScroller]
                && document
                && closeEnough([document frame].size.width, nativeWidth)
                && closeEnough([document frame].size.height, nativeHeight);
        } else {
            ok = ok && document
                && closeEnough([document frame].size.width, nativeWidth)
                && closeEnough([document frame].size.height, nativeHeight);
        }
        if (ok) {
            NSData* rendered = [document dataWithPDFInsideRect:[document bounds]];
            ok = rendered && [rendered length] > 0u;
            if (ok) {
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                if (captureDirectory && captureDirectory[0]) {
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil
                        error:nil];
                    NSString* fileName = [[NSString
                        stringWithUTF8String:pluginId]
                        stringByAppendingPathExtension:@"pdf"];
                    ok = [rendered writeToFile:
                        [directory stringByAppendingPathComponent:fileName]
                        atomically:YES];
                }
            }
        }
        if (ok && responsive) {
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
                && (!responsive
                    || teardownGui->set_size(teardownPlugin, testWidth, testHeight))
                && teardownGui->set_parent(teardownPlugin, &teardownWindow);
            if (ok && responsive) {
                ok = teardownGui->show(teardownPlugin);
            }
            if (responsive) {
                teardownPlugin->destroy(teardownPlugin);
            } else {
                // Fixed legacy family members currently rely on the host's
                // conventional gui.destroy() before plug-in destruction.
                teardownGui->destroy(teardownPlugin);
                teardownPlugin->destroy(teardownPlugin);
            }
            ok = ok && [[teardownParent subviews] count] == 0u;
            [teardownParent release];
        } else {
            ok = false;
        }
        entry->deinit();
        // Keep the bundle loaded: Objective-C classes remain registered for the
        // process lifetime, which mirrors production plug-in hosts.

        if (!ok) {
            std::cerr << (responsive ? "Responsive" : "Fixed")
                << " GUI smoke failed for " << pluginId << "\n";
            return 1;
        }
        std::cout << (responsive ? "Responsive" : "Fixed")
            << " GUI smoke passed for " << pluginId << "\n";
    }
    return 0;
}
