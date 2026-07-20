#include "s3g_ambi_imprint.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/latency.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#if defined(__APPLE__)
#include <clap/ext/gui.h>
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kChannels = 64u;
constexpr uint32_t kGuiWidth = 900u;
constexpr uint32_t kGuiHeight = 480u;
constexpr uint32_t kStateMagic = 0x5347494du;
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kGuiStateMagic = 0x53474956u;
constexpr uint32_t kGuiStateVersion = 1u;
constexpr uint32_t kMaximumStateJsonBytes = 2u * 1024u * 1024u;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-imprint-64";
constexpr const char* kPluginName = "s3g Ambi Imprint 64";
constexpr const char* kPluginDesc = "64-channel ambisonic directional imprint convolution.";

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamMix = 2,
    kParamFocus = 3,
    kParamWidth = 4,
    kParamOutput = 5,
    kParamBypass = 6,
};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    s3g::AmbiImprintParams params {};
    uint32_t jsonBytes = 0u;
};

struct SavedGuiStateTail {
    uint32_t magic = kGuiStateMagic;
    uint32_t version = kGuiStateVersion;
    int32_t viewMode = 2;
    double viewAzimuthDeg = 35.0;
    double viewElevationDeg = 34.0;
    double viewZoom = 1.0;
};

struct GuiProfileSnapshot {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float sourceX = 0.0f;
    float sourceY = 0.0f;
    float sourceZ = 0.0f;
    float rt60 = 0.0f;
    uint32_t reflections = 0u;
};

struct GuiBranchGeometry {
    std::string family = "room";
    std::string attachment = "wall";
    uint32_t level = 0u;
    float baseHeight = 0.0f;
    float height = 0.0f;
    std::vector<std::array<float, 2>> polygon;
};

struct GuiPortalGeometry {
    std::string shape = "rect";
    std::array<float, 3> center {};
    std::array<float, 3> normal { 0.0f, 1.0f, 0.0f };
    std::array<float, 3> axisU { 1.0f, 0.0f, 0.0f };
    std::array<float, 3> axisV { 0.0f, 0.0f, 1.0f };
    float width = 1.0f;
    float height = 1.0f;
    std::vector<std::array<float, 3>> outline;
};

struct GuiSpaceGeometry {
    std::string family = "room";
    std::vector<std::array<float, 2>> ceilingProfile;
    std::vector<GuiBranchGeometry> branches;
    std::vector<GuiPortalGeometry> portals;
};

struct GuiSnapshot {
    std::string name;
    std::string status;
    float roomWidth = 8.0f;
    float roomDepth = 10.0f;
    float roomHeight = 3.0f;
    float listenerX = 4.0f;
    float listenerY = 5.0f;
    float listenerZ = 1.5f;
    float duration = 0.0f;
    std::string family = "room";
    std::vector<std::array<float, 2>> polygon;
    std::vector<std::array<float, 2>> ceilingProfile;
    std::vector<GuiBranchGeometry> branches;
    std::vector<GuiPortalGeometry> portals;
    std::array<GuiProfileSnapshot, s3g::kAmbiImprintMaxProfiles> profiles {};
    uint32_t profileCount = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    s3g::AmbiImprintParams params {};
    s3g::AmbiImprintDescriptor descriptor {};
    GuiSpaceGeometry guiGeometry {};
    std::string imprintJson;
    std::string imprintName = "NO IMPRINT";
    std::string status = "LOAD .S3GIMPRINT";
    std::mutex stateMutex;
    std::vector<std::unique_ptr<s3g::AmbiImprintProcessor>> runtimes;
    std::atomic<s3g::AmbiImprintProcessor*> activeProcessor { nullptr };
    std::atomic<bool> active { false };
    std::atomic<bool> hasImprint { false };
    std::atomic<float> outputPeak { 0.0f };
    double sampleRate = 48000.0;
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
    int guiViewMode = 2;
    double guiViewAzimuthDeg = 35.0;
    double guiViewElevationDeg = 34.0;
    double guiViewZoom = 1.0;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool streamWriteAll(const clap_ostream_t* stream, const void* source, uint64_t bytes)
{
    const auto* data = static_cast<const uint8_t*>(source);
    uint64_t position = 0u;
    while (position < bytes) {
        const int64_t wrote = stream->write(stream, data + position, bytes - position);
        if (wrote <= 0) return false;
        position += static_cast<uint64_t>(wrote);
    }
    return true;
}

bool streamReadAll(const clap_istream_t* stream, void* destination, uint64_t bytes)
{
    auto* data = static_cast<uint8_t*>(destination);
    uint64_t position = 0u;
    while (position < bytes) {
        const int64_t got = stream->read(stream, data + position, bytes - position);
        if (got <= 0) return false;
        position += static_cast<uint64_t>(got);
    }
    return true;
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kParamOrder: plugin.params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kParamMix: plugin.params.mix = static_cast<float>(value); break;
    case kParamFocus: plugin.params.focus = static_cast<float>(value); break;
    case kParamWidth: plugin.params.width = static_cast<float>(value); break;
    case kParamOutput: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kParamBypass: plugin.params.bypass = value >= 0.5; break;
    default: return;
    }
    plugin.params = s3g::sanitizeAmbiImprintParams(plugin.params);
    if (auto* processor = plugin.activeProcessor.load(std::memory_order_acquire)) processor->setParams(plugin.params);
}

double getParam(const Plugin& plugin, clap_id id)
{
    switch (id) {
    case kParamOrder: return plugin.params.order;
    case kParamMix: return plugin.params.mix;
    case kParamFocus: return plugin.params.focus;
    case kParamWidth: return plugin.params.width;
    case kParamOutput: return plugin.params.outputGainDb;
    case kParamBypass: return plugin.params.bypass ? 1.0 : 0.0;
    default: return 0.0;
    }
}

bool buildRuntime(Plugin& plugin, const s3g::AmbiImprintDescriptor& descriptor, std::string& error)
{
    auto runtime = std::make_unique<s3g::AmbiImprintProcessor>();
    runtime->setParams(plugin.params);
    if (!runtime->prepare(plugin.sampleRate, descriptor)) {
        error = "FFT KERNEL BUILD FAILED";
        return false;
    }
    auto* next = runtime.get();
    plugin.runtimes.push_back(std::move(runtime));
    plugin.activeProcessor.store(next, std::memory_order_release);
    return true;
}

bool installDescriptor(Plugin& plugin,
                       s3g::AmbiImprintDescriptor descriptor,
                       GuiSpaceGeometry geometry,
                       std::string json,
                       std::string name,
                       std::string& error)
{
    descriptor = s3g::sanitizeAmbiImprintDescriptor(std::move(descriptor));
    if (descriptor.profiles.empty()) {
        error = "IMPRINT HAS NO PROFILES";
        return false;
    }
    if (plugin.active.load(std::memory_order_acquire) && !buildRuntime(plugin, descriptor, error)) return false;
    {
        std::lock_guard<std::mutex> lock(plugin.stateMutex);
        plugin.descriptor = std::move(descriptor);
        plugin.guiGeometry = std::move(geometry);
        plugin.imprintJson = std::move(json);
        plugin.imprintName = std::move(name);
        plugin.status = "READY";
    }
    plugin.hasImprint.store(true, std::memory_order_release);
    if (plugin.hostTail && plugin.host) plugin.hostTail->changed(plugin.host);
    return true;
}

GuiSnapshot guiSnapshot(Plugin& plugin)
{
    GuiSnapshot result;
    std::lock_guard<std::mutex> lock(plugin.stateMutex);
    result.name = plugin.imprintName;
    result.status = plugin.status;
    result.roomWidth = plugin.descriptor.room.widthMetres;
    result.roomDepth = plugin.descriptor.room.depthMetres;
    result.roomHeight = plugin.descriptor.room.heightMetres;
    result.listenerX = plugin.descriptor.listenerPositionMetres[0];
    result.listenerY = plugin.descriptor.listenerPositionMetres[1];
    result.listenerZ = plugin.descriptor.listenerPositionMetres[2];
    result.duration = plugin.hasImprint.load(std::memory_order_relaxed) ? std::min(plugin.descriptor.durationSeconds, s3g::kAmbiImprintMaxKernelSeconds) : 0.0f;
    result.family = plugin.guiGeometry.family;
    result.polygon = plugin.descriptor.room.polygon;
    result.ceilingProfile = plugin.guiGeometry.ceilingProfile;
    result.branches = plugin.guiGeometry.branches;
    result.portals = plugin.guiGeometry.portals;
    result.profileCount = std::min<uint32_t>(static_cast<uint32_t>(plugin.descriptor.profiles.size()), s3g::kAmbiImprintMaxProfiles);
    for (uint32_t i = 0; i < result.profileCount; ++i) {
        const auto& source = plugin.descriptor.profiles[i];
        auto& target = result.profiles[i];
        target.azimuthDeg = source.azimuthDeg;
        target.elevationDeg = source.elevationDeg;
        target.sourceX = source.sourceXMetres;
        target.sourceY = source.sourceYMetres;
        target.sourceZ = source.sourceZMetres;
        target.reflections = static_cast<uint32_t>(source.earlyReflections.size());
        float rt60 = 0.0f;
        for (float value : source.late.rt60SecondsByBand) rt60 += value;
        target.rt60 = rt60 / static_cast<float>(s3g::kAmbiImprintBands);
    }
    return result;
}

#if defined(__APPLE__)
float numberValue(NSDictionary* dictionary, NSString* key, float fallback)
{
    id value = [dictionary objectForKey:key];
    return [value respondsToSelector:@selector(doubleValue)] ? static_cast<float>([value doubleValue]) : fallback;
}

uint32_t unsignedValue(NSDictionary* dictionary, NSString* key, uint32_t fallback)
{
    id value = [dictionary objectForKey:key];
    return [value respondsToSelector:@selector(unsignedIntValue)] ? static_cast<uint32_t>([value unsignedIntValue]) : fallback;
}

NSDictionary* dictionaryValue(NSDictionary* dictionary, NSString* key)
{
    id value = [dictionary objectForKey:key];
    return [value isKindOfClass:[NSDictionary class]] ? static_cast<NSDictionary*>(value) : nil;
}

NSArray* arrayValue(NSDictionary* dictionary, NSString* key)
{
    id value = [dictionary objectForKey:key];
    return [value isKindOfClass:[NSArray class]] ? static_cast<NSArray*>(value) : nil;
}

std::string stringValue(NSDictionary* dictionary, NSString* key, const char* fallback = "")
{
    id value = [dictionary objectForKey:key];
    if (![value isKindOfClass:[NSString class]]) return fallback ? std::string(fallback) : std::string {};
    const char* text = [static_cast<NSString*>(value) UTF8String];
    return text ? std::string(text) : (fallback ? std::string(fallback) : std::string {});
}

std::vector<std::array<float, 2>> parsePointPairs(NSArray* values, NSString* secondKey, uint32_t maximum = 64u)
{
    std::vector<std::array<float, 2>> result;
    if (![values isKindOfClass:[NSArray class]]) return result;
    const uint32_t count = std::min<uint32_t>(maximum, static_cast<uint32_t>([values count]));
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        id item = [values objectAtIndex:i];
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        auto* point = static_cast<NSDictionary*>(item);
        result.push_back({ numberValue(point, @"x", 0.0f), numberValue(point, secondKey, 0.0f) });
    }
    return result;
}

std::array<float, 3> parseVector3(NSDictionary* value, std::array<float, 3> fallback = {})
{
    if (![value isKindOfClass:[NSDictionary class]]) return fallback;
    return { numberValue(value, @"x", fallback[0]),
             numberValue(value, @"y", fallback[1]),
             numberValue(value, @"z", fallback[2]) };
}

std::vector<std::array<float, 3>> parsePointTriples(NSArray* values, uint32_t maximum = 48u)
{
    std::vector<std::array<float, 3>> result;
    if (![values isKindOfClass:[NSArray class]]) return result;
    const uint32_t count = std::min<uint32_t>(maximum, static_cast<uint32_t>([values count]));
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        id item = [values objectAtIndex:i];
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        result.push_back(parseVector3(static_cast<NSDictionary*>(item)));
    }
    return result;
}

float defaultBranchHeight(float roomHeight, const std::string& family, uint32_t level)
{
    float ratio = 0.72f;
    if (family == "cave") ratio = 0.66f;
    else if (family == "cavern") ratio = 1.02f;
    else if (family == "tunnel") ratio = 0.52f;
    else if (family == "canyon") ratio = 1.10f;
    else if (family == "clearing") ratio = 0.34f;
    else if (family == "abstract") ratio = 0.82f;
    return roomHeight * std::clamp(ratio + static_cast<float>(level) * 0.05f, 0.28f, 1.18f);
}

bool parseBandArray(NSArray* values, std::array<float, s3g::kAmbiImprintBands>& destination)
{
    if (![values isKindOfClass:[NSArray class]] || [values count] != s3g::kAmbiImprintBands) return false;
    for (uint32_t i = 0; i < s3g::kAmbiImprintBands; ++i) {
        id value = [values objectAtIndex:i];
        if (![value respondsToSelector:@selector(doubleValue)]) return false;
        destination[i] = static_cast<float>([value doubleValue]);
    }
    return true;
}

bool parseImprintData(NSData* data,
                      s3g::AmbiImprintDescriptor& descriptor,
                      GuiSpaceGeometry& geometry,
                      std::string& json,
                      std::string& error)
{
    if (!data || [data length] == 0u || [data length] > kMaximumStateJsonBytes) {
        error = "FILE SIZE IS INVALID";
        return false;
    }
    NSError* parseError = nil;
    id rootObject = [NSJSONSerialization JSONObjectWithData:data options:0 error:&parseError];
    if (parseError || ![rootObject isKindOfClass:[NSDictionary class]]) {
        error = "INVALID JSON";
        return false;
    }
    NSDictionary* root = static_cast<NSDictionary*>(rootObject);
    NSString* format = [root objectForKey:@"format"];
    if (![format isKindOfClass:[NSString class]] || ![format isEqualToString:@"s3g-ambi-imprint"]) {
        error = "NOT AN S3G IMPRINT";
        return false;
    }
    if (unsignedValue(root, @"version", 0u) != s3g::kAmbiImprintFormatVersion) {
        error = "UNSUPPORTED FORMAT VERSION";
        return false;
    }
    NSArray* profiles = arrayValue(root, @"profiles");
    if (!profiles || [profiles count] == 0u || [profiles count] > s3g::kAmbiImprintMaxProfiles) {
        error = "EXPECTED 1-8 PROFILES";
        return false;
    }

    s3g::AmbiImprintDescriptor parsed;
    GuiSpaceGeometry parsedGeometry;
    parsed.version = s3g::kAmbiImprintFormatVersion;
    parsed.durationSeconds = numberValue(root, @"duration_s", 2.0f);
    if (NSDictionary* ambisonics = dictionaryValue(root, @"ambisonics")) parsed.referenceOrder = unsignedValue(ambisonics, @"reference_order", 3u);
    std::vector<std::array<float, 2>> primarySpacePolygon;
    if (NSDictionary* space = dictionaryValue(root, @"space")) {
        parsedGeometry.family = stringValue(space, @"family", "room");
        primarySpacePolygon = parsePointPairs(arrayValue(space, @"primary_polygon_xy_m"), @"y");
        parsedGeometry.ceilingProfile = parsePointPairs(arrayValue(space, @"ceiling_profile_xz_m"), @"z");
        if (NSArray* regions = arrayValue(space, @"regions")) {
            const uint32_t count = std::min<uint32_t>(64u, static_cast<uint32_t>([regions count]));
            parsedGeometry.branches.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                id item = [regions objectAtIndex:i];
                if (![item isKindOfClass:[NSDictionary class]]) continue;
                auto* source = static_cast<NSDictionary*>(item);
                if (stringValue(source, @"kind", "branch") == "primary") continue;
                GuiBranchGeometry branch;
                branch.family = stringValue(source, @"family", parsedGeometry.family.c_str());
                branch.attachment = stringValue(source, @"attachment", "wall");
                branch.level = unsignedValue(source, @"level", 0u);
                branch.baseHeight = numberValue(source, @"base_z_m", 0.0f);
                branch.height = numberValue(source, @"height_m", defaultBranchHeight(parsed.room.heightMetres, branch.family, branch.level));
                branch.polygon = parsePointPairs(arrayValue(source, @"polygon_xy_m"), @"y");
                if (branch.polygon.size() >= 3u) parsedGeometry.branches.push_back(std::move(branch));
            }
        }
        if (NSArray* portals = arrayValue(space, @"portals")) {
            const uint32_t count = std::min<uint32_t>(64u, static_cast<uint32_t>([portals count]));
            parsedGeometry.portals.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                id item = [portals objectAtIndex:i];
                if (![item isKindOfClass:[NSDictionary class]]) continue;
                auto* source = static_cast<NSDictionary*>(item);
                GuiPortalGeometry portal;
                portal.shape = stringValue(source, @"shape", "rect");
                portal.center = parseVector3(dictionaryValue(source, @"center_m"));
                portal.normal = parseVector3(dictionaryValue(source, @"normal"), { 0.0f, 1.0f, 0.0f });
                portal.axisU = parseVector3(dictionaryValue(source, @"axis_u"), { 1.0f, 0.0f, 0.0f });
                portal.axisV = parseVector3(dictionaryValue(source, @"axis_v"), { 0.0f, 0.0f, 1.0f });
                portal.width = std::max(0.01f, numberValue(source, @"width_m", 1.0f));
                portal.height = std::max(0.01f, numberValue(source, @"height_m", 1.0f));
                portal.outline = parsePointTriples(arrayValue(source, @"outline_xyz_m"));
                if (portal.outline.size() < 3u) {
                    for (const auto& corner : std::array<std::array<float, 2>, 4> {
                             std::array<float, 2> { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } }) {
                        portal.outline.push_back({
                            portal.center[0] + portal.axisU[0] * corner[0] * portal.width + portal.axisV[0] * corner[1] * portal.height,
                            portal.center[1] + portal.axisU[1] * corner[0] * portal.width + portal.axisV[1] * corner[1] * portal.height,
                            portal.center[2] + portal.axisU[2] * corner[0] * portal.width + portal.axisV[2] * corner[1] * portal.height
                        });
                    }
                }
                parsedGeometry.portals.push_back(std::move(portal));
            }
        }
    }
    if (NSDictionary* room = dictionaryValue(root, @"room")) {
        if (parsedGeometry.family.empty() || parsedGeometry.family == "room") parsedGeometry.family = stringValue(room, @"family", parsedGeometry.family.c_str());
        if (NSDictionary* dimensions = dictionaryValue(room, @"dimensions_m")) {
            parsed.room.widthMetres = numberValue(dimensions, @"x", 8.0f);
            parsed.room.depthMetres = numberValue(dimensions, @"y", 10.0f);
            parsed.room.heightMetres = numberValue(dimensions, @"z", 3.0f);
        }
        parsed.room.polygon = parsePointPairs(arrayValue(room, @"polygon_xy_m"), @"y");
        if (parsedGeometry.branches.empty()) if (NSDictionary* chamber = dictionaryValue(room, @"chamber")) {
            if (NSArray* branches = arrayValue(chamber, @"chambers")) {
                const uint32_t count = std::min<uint32_t>(64u, static_cast<uint32_t>([branches count]));
                parsedGeometry.branches.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    id item = [branches objectAtIndex:i];
                    if (![item isKindOfClass:[NSDictionary class]]) continue;
                    auto* source = static_cast<NSDictionary*>(item);
                    GuiBranchGeometry branch;
                    branch.family = stringValue(source, @"family", parsedGeometry.family.c_str());
                    branch.attachment = stringValue(source, @"attachment", stringValue(source, @"side", "wall").c_str());
                    branch.level = unsignedValue(source, @"level", 0u);
                    branch.baseHeight = numberValue(source, @"base_z_m", 0.0f);
                    branch.height = numberValue(source, @"height_m", defaultBranchHeight(parsed.room.heightMetres, branch.family, branch.level));
                    branch.polygon = parsePointPairs(arrayValue(source, @"polygon"), @"y");
                    if (branch.polygon.size() >= 3u) parsedGeometry.branches.push_back(std::move(branch));
                }
            }
        }
    }
    if (parsed.room.polygon.size() < 3u && primarySpacePolygon.size() >= 3u) parsed.room.polygon = std::move(primarySpacePolygon);
    if (parsedGeometry.ceilingProfile.size() < 2u) {
        parsedGeometry.ceilingProfile = {
            { 0.0f, parsed.room.heightMetres },
            { parsed.room.widthMetres, parsed.room.heightMetres }
        };
    }
    if (NSDictionary* listener = dictionaryValue(root, @"listener_position_m")) {
        parsed.listenerPositionMetres = { numberValue(listener, @"x", parsed.room.widthMetres * 0.5f),
                                          numberValue(listener, @"y", parsed.room.depthMetres * 0.5f),
                                          numberValue(listener, @"z", parsed.room.heightMetres * 0.5f) };
    }

    for (id item in profiles) {
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        auto* source = static_cast<NSDictionary*>(item);
        s3g::AmbiImprintProfile profile;
        if (NSDictionary* direction = dictionaryValue(source, @"input_direction")) {
            profile.azimuthDeg = numberValue(direction, @"azimuth_deg", 0.0f);
            profile.elevationDeg = numberValue(direction, @"elevation_deg", 0.0f);
        }
        if (NSDictionary* position = dictionaryValue(source, @"source_position_m")) {
            profile.sourceXMetres = numberValue(position, @"x", 0.0f);
            profile.sourceYMetres = numberValue(position, @"y", 0.0f);
            profile.sourceZMetres = numberValue(position, @"z", 0.0f);
        }
        profile.weight = numberValue(source, @"weight", 1.0f / static_cast<float>([profiles count]));
        if (NSDictionary* direct = dictionaryValue(source, @"direct")) {
            profile.directDelayMs = numberValue(direct, @"delay_ms", 0.0f);
            profile.directGain = numberValue(direct, @"gain", 1.0f);
        }
        if (NSArray* reflections = arrayValue(source, @"early_reflections")) {
            const uint32_t count = std::min<uint32_t>(128u, static_cast<uint32_t>([reflections count]));
            profile.earlyReflections.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                id reflectionObject = [reflections objectAtIndex:i];
                if (![reflectionObject isKindOfClass:[NSDictionary class]]) continue;
                auto* reflection = static_cast<NSDictionary*>(reflectionObject);
                profile.earlyReflections.push_back({ numberValue(reflection, @"delay_ms", 0.0f),
                                                     numberValue(reflection, @"gain", 0.0f),
                                                     numberValue(reflection, @"azimuth_deg", 0.0f),
                                                     numberValue(reflection, @"elevation_deg", 0.0f) });
            }
        }
        NSDictionary* late = dictionaryValue(source, @"late");
        if (!late
            || !parseBandArray(arrayValue(late, @"absorption_by_band"), profile.late.absorptionByBand)
            || !parseBandArray(arrayValue(late, @"rt60_s_by_band"), profile.late.rt60SecondsByBand)) {
            error = "PROFILE BAND DATA IS INVALID";
            return false;
        }
        profile.late.startMs = numberValue(late, @"start_ms", 40.0f);
        profile.late.durationSeconds = numberValue(late, @"duration_s", parsed.durationSeconds);
        profile.late.level = numberValue(late, @"level", 0.15f);
        profile.late.diffusion = numberValue(late, @"diffusion", 0.5f);
        profile.late.spreadDeg = numberValue(late, @"spread_deg", 45.0f);
        profile.late.highFrequencyDamping = numberValue(late, @"high_frequency_damping", 0.4f);
        profile.late.seed = unsignedValue(late, @"seed", 1u);
        parsed.profiles.push_back(std::move(profile));
    }
    if (parsed.profiles.empty()) {
        error = "NO VALID PROFILES";
        return false;
    }
    descriptor = s3g::sanitizeAmbiImprintDescriptor(std::move(parsed));
    geometry = std::move(parsedGeometry);
    NSString* text = [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
    if (!text) {
        error = "JSON IS NOT UTF-8";
        return false;
    }
    json.assign([text UTF8String], [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    return true;
}
#endif

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
#if defined(__APPLE__)
    if (instance->guiView) {
        NSView* view = static_cast<NSView*>(instance->guiView);
        [view removeFromSuperview];
        [view release];
        instance->guiView = nullptr;
    }
#endif
    delete instance;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    instance->runtimes.clear();
    instance->activeProcessor.store(nullptr, std::memory_order_release);
    s3g::AmbiImprintDescriptor descriptor;
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        descriptor = instance->hasImprint.load(std::memory_order_relaxed) ? instance->descriptor : s3g::AmbiImprintDescriptor {};
        if (!instance->hasImprint.load(std::memory_order_relaxed)) descriptor.profiles.clear();
    }
    std::string error;
    if (!buildRuntime(*instance, descriptor, error)) return false;
    instance->active.store(true, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->active.store(false, std::memory_order_release);
    instance->activeProcessor.store(nullptr, std::memory_order_release);
    instance->runtimes.clear();
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) processor->reset();
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* inputEvents)
{
    if (!inputEvents) return;
    const uint32_t count = inputEvents->size(inputEvents);
    for (uint32_t i = 0; i < count; ++i) {
        const auto* event = inputEvents->get(inputEvents, i);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, value->param_id, value->value);
        }
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& plugin,
                                 const clap_audio_buffer_t& input,
                                 const clap_audio_buffer_t& output,
                                 uint32_t frames,
                                 Sample** inputData,
                                 Sample** outputData)
{
    s3g::clearAudioBuffer(output, frames);
    auto* processor = plugin.activeProcessor.load(std::memory_order_acquire);
    if (!processor || !inputData || !outputData) return CLAP_PROCESS_CONTINUE;
    processor->setParams(plugin.params);
    processor->process(inputData, input.channel_count, outputData, output.channel_count, frames);
    float peak = 0.0f;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kChannels);
    for (uint32_t channel = 0; channel < channels; ++channel) {
        if (!outputData[channel]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, static_cast<float>(std::abs(outputData[channel][frame])));
    }
    const float previous = plugin.outputPeak.load(std::memory_order_relaxed);
    plugin.outputPeak.store(std::max(previous * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, process->in_events);
    if (process->audio_inputs_count == 0u || process->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    if (input.data32 && output.data32) return processTyped<float>(*instance, input, output, process->frames_count, input.data32, output.data32);
    if (input.data64 && output.data64) return processTyped<double>(*instance, input, output, process->frames_count, input.data64, output.data64);
    s3g::clearAudioBuffer(output, process->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name, isInput ? "Ambi Imprint In" : "Ambi Imprint Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 6u; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Ambi Imprint", sizeof(info->module));
    switch (index) {
    case 0: info->id = kParamOrder; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Order", sizeof(info->name)); info->min_value = 1; info->max_value = 7; info->default_value = 7; return true;
    case 1: info->id = kParamMix; std::strncpy(info->name, "Mix", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.5; return true;
    case 2: info->id = kParamFocus; std::strncpy(info->name, "Directional focus", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 1; return true;
    case 3: info->id = kParamWidth; std::strncpy(info->name, "Wet order width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; return true;
    case 4: info->id = kParamOutput; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; return true;
    case 5: info->id = kParamBypass; info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS; std::strncpy(info->name, "Bypass", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0; return true;
    default: return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value || paramId < kParamOrder || paramId > kParamBypass) return false;
    *value = getParam(*self(plugin), paramId);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    switch (paramId) {
    case kParamOrder: std::snprintf(display, size, "%.0fOA", value); return true;
    case kParamMix:
    case kParamFocus: std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    case kParamWidth: std::snprintf(display, size, "%.2f", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    case kParamBypass: std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off"); return true;
    default: return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value || paramId < kParamOrder || paramId > kParamBypass) return false;
    switch (paramId) {
    case kParamMix:
    case kParamFocus: *value = std::atof(display) * 0.01; break;
    case kParamBypass:
        *value = (display[0] == 'O' || display[0] == 'o')
            && (display[1] == 'N' || display[1] == 'n') ? 1.0 : 0.0;
        break;
    default: *value = std::atof(display); break;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool readOptionalGuiState(const clap_istream_t* stream, SavedGuiStateTail& state, bool& present)
{
    present = false;
    auto* bytes = reinterpret_cast<uint8_t*>(&state);
    size_t position = 0u;
    while (position < sizeof(state.magic)) {
        const int64_t got = stream->read(stream, bytes + position, sizeof(state.magic) - position);
        if (got == 0) return position == 0u;
        if (got < 0) return false;
        position += static_cast<size_t>(got);
    }
    if (state.magic != kGuiStateMagic) return true;
    if (!streamReadAll(stream, bytes + sizeof(state.magic), sizeof(state) - sizeof(state.magic))) return false;
    present = state.version == kGuiStateVersion;
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* instance = self(plugin);
    std::string json;
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        json = instance->imprintJson;
    }
    if (json.size() > kMaximumStateJsonBytes) return false;
    const SavedStateHeader header { kStateMagic, kStateVersion, instance->params, static_cast<uint32_t>(json.size()) };
    SavedGuiStateTail guiState;
    std::memset(&guiState, 0, sizeof(guiState));
    guiState.magic = kGuiStateMagic;
    guiState.version = kGuiStateVersion;
    guiState.viewMode = 2;
    guiState.viewAzimuthDeg = 35.0;
    guiState.viewElevationDeg = 34.0;
    guiState.viewZoom = 1.0;
#if defined(__APPLE__)
    guiState.viewMode = instance->guiViewMode;
    guiState.viewAzimuthDeg = instance->guiViewAzimuthDeg;
    guiState.viewElevationDeg = instance->guiViewElevationDeg;
    guiState.viewZoom = instance->guiViewZoom;
#endif
    return streamWriteAll(stream, &header, sizeof(header))
        && (json.empty() || streamWriteAll(stream, json.data(), json.size()))
        && streamWriteAll(stream, &guiState, sizeof(guiState));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedStateHeader header;
    if (!streamReadAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic
        || header.version != kStateVersion
        || header.jsonBytes > kMaximumStateJsonBytes) return false;
    auto* instance = self(plugin);
    instance->params = s3g::sanitizeAmbiImprintParams(header.params);
    if (header.jsonBytes > 0u) {
        std::string json(header.jsonBytes, '\0');
        if (!streamReadAll(stream, json.data(), json.size())) return false;
#if defined(__APPLE__)
        NSData* data = [NSData dataWithBytes:json.data() length:json.size()];
        s3g::AmbiImprintDescriptor descriptor;
        GuiSpaceGeometry geometry;
        std::string canonical;
        std::string error;
        if (!parseImprintData(data, descriptor, geometry, canonical, error)) return false;
        if (!installDescriptor(*instance, std::move(descriptor), std::move(geometry), std::move(canonical), "PROJECT STATE", error)) return false;
#else
        return false;
#endif
    }
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) processor->setParams(instance->params);
    SavedGuiStateTail guiState;
    bool hasGuiState = false;
    if (!readOptionalGuiState(stream, guiState, hasGuiState)) return false;
#if defined(__APPLE__)
    if (hasGuiState) {
        instance->guiViewMode = std::clamp<int>(guiState.viewMode, -1, 2);
        instance->guiViewAzimuthDeg = std::clamp(guiState.viewAzimuthDeg, -180.0, 180.0);
        instance->guiViewElevationDeg = std::clamp(guiState.viewElevationDeg, -89.0, 89.0);
        instance->guiViewZoom = std::clamp(guiState.viewZoom, 0.55, 2.20);
    }
#endif
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t latencyGet(const clap_plugin_t*) { return s3g::kAmbiImprintPartitionSize; }
const clap_plugin_latency_t latencyExt { latencyGet };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->hasImprint.load(std::memory_order_relaxed) || instance->params.bypass) return 0u;
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) return processor->tailFrames();
    return 0u;
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
namespace {

float linearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

NSColor* imprintPointColor(float azimuthDeg, float elevationDeg)
{
    const float hue = std::fmod(azimuthDeg / 360.0f + 1.0f, 1.0f);
    const float light = std::clamp((elevationDeg + 90.0f) / 180.0f, 0.34f, 0.78f);
    const float chroma = 0.24f;
    const float a = std::cos(hue * 2.0f * s3g::kPi) * chroma;
    const float b = std::sin(hue * 2.0f * s3g::kPi) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    const float red = linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    const float green = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    const float blue = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    return [NSColor colorWithCalibratedRed:red green:green blue:blue alpha:0.88];
}

NSString* compactFileName(const std::string& name)
{
    NSString* value = [NSString stringWithUTF8String:name.c_str()];
    if (!value) return @"-";
    if ([value length] <= 27u) return value;
    return [NSString stringWithFormat:@"...%@", [value substringFromIndex:[value length] - 24u]];
}

struct ImprintAtlasEntry {
    const char* title;
    const char* resource;
};

constexpr std::array<ImprintAtlasEntry, 19> kImprintAtlas {{
    { "ARCH / CONCRETE GALLERY", "architecture_concrete_gallery" },
    { "ARCH / WOOD STUDIO", "architecture_wood_studio" },
    { "CAVE / LIMESTONE POCKET", "cave_limestone_pocket" },
    { "CAVE / ICE GROTTO", "cave_ice_grotto" },
    { "CAVERN / DEEP STONE VAULT", "cavern_deep_stone_vault" },
    { "CAVERN / WATER CHAMBER", "cavern_water_chamber" },
    { "TUNNEL / BRICK BEND", "tunnel_brick_bend" },
    { "TUNNEL / METAL CONDUIT", "tunnel_metal_conduit" },
    { "CANYON / DRY SLOT", "canyon_dry_slot" },
    { "CANYON / POROUS GORGE", "canyon_porous_gorge" },
    { "CLEAR / FOREST RING", "clearing_forest_ring" },
    { "CLEAR / WATER MEADOW", "clearing_water_meadow" },
    { "ECHO / STONE FLUTTER", "echo_stone_flutter_gallery" },
    { "ECHO / METAL AXIAL TUNNEL", "echo_metal_axial_tunnel" },
    { "ECHO / TWIN CHAMBERS", "echo_twin_concrete_chambers" },
    { "ECHO / PERIMETER CIRCUIT", "echo_stone_perimeter_circuit" },
    { "ECHO / IMPOSSIBLE RELAY", "echo_impossible_relay" },
    { "ABSTRACT / FOLDED CHAMBER", "abstract_folded_chamber" },
    { "ABSTRACT / IMPOSSIBLE NETWORK", "abstract_impossible_network" },
}};

NSRect imprintAtlasMenuRect()
{
    return NSMakeRect(616, 56, 272, 18.0 * static_cast<CGFloat>(kImprintAtlas.size()));
}

} // namespace

@interface S3GAmbiImprintView : NSView {
    Plugin* _plugin;
    clap_id _dragParam;
    NSTimer* _refreshTimer;
    bool _orderMenuOpen;
    int _orderMenuHover;
    bool _atlasMenuOpen;
    int _atlasMenuHover;
    int _viewMode;
    CGFloat _viewAzimuthDeg;
    CGFloat _viewElevationDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    NSPoint _lastDragPoint;
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)loadImprint;
- (void)loadAtlasAtIndex:(NSUInteger)index;
- (void)storeViewState;
- (NSRect)viewButtonRect:(int)index;
- (NSRect)zoomButtonRect:(int)index;
- (void)setViewPreset:(int)mode;
- (void)updateSliderAtPoint:(NSPoint)point;
@end

@implementation S3GAmbiImprintView

- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _refreshTimer = nil;
        _orderMenuOpen = false;
        _orderMenuHover = -1;
        _atlasMenuOpen = false;
        _atlasMenuHover = -1;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzimuthDeg = plugin ? plugin->guiViewAzimuthDeg : 35.0;
        _viewElevationDeg = plugin ? plugin->guiViewElevationDeg : 34.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
    }
    return self;
}

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzimuthDeg = _viewAzimuthDeg;
    _plugin->guiViewElevationDeg = _viewElevationDeg;
    _plugin->guiViewZoom = _viewZoom;
}

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) [self removeTrackingArea:area];
    NSTrackingArea* area = [[[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow
        owner:self
        userInfo:nil] autorelease];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 24.0) target:self selector:@selector(refreshTimerFired:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_refreshTimer) {
        [_refreshTimer invalidate];
        _refreshTimer = nil;
    }
}

- (void)refreshTimerFired:(NSTimer*)timer
{
    (void)timer;
    if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}

- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*_plugin, param, value);
    [self setNeedsDisplay:YES];
}

- (void)setStatus:(const std::string&)status
{
    std::lock_guard<std::mutex> lock(_plugin->stateMutex);
    _plugin->status = status;
}

- (void)loadImprint
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"s3gimprint"]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    NSData* data = [NSData dataWithContentsOfURL:[panel URL]];
    s3g::AmbiImprintDescriptor descriptor;
    GuiSpaceGeometry geometry;
    std::string json;
    std::string error;
    if (!parseImprintData(data, descriptor, geometry, json, error)) {
        [self setStatus:error];
        [self setNeedsDisplay:YES];
        return;
    }
    NSString* lastPath = [[panel URL] lastPathComponent];
    const std::string name = lastPath ? std::string([lastPath UTF8String]) : std::string("IMPRINT");
    [self setStatus:"BUILDING KERNELS"];
    [self displayIfNeeded];
    if (!installDescriptor(*_plugin, std::move(descriptor), std::move(geometry), std::move(json), name, error)) [self setStatus:error];
    [self setNeedsDisplay:YES];
}

- (void)loadAtlasAtIndex:(NSUInteger)index
{
    if (index >= kImprintAtlas.size()) return;
    const auto& entry = kImprintAtlas[index];
    NSString* resource = [NSString stringWithUTF8String:entry.resource];
    NSBundle* bundle = [NSBundle bundleForClass:[self class]];
    NSString* path = [bundle pathForResource:resource ofType:@"s3gimprint" inDirectory:@"Imprint Atlas"];
    if (!path) path = [[NSBundle mainBundle] pathForResource:resource ofType:@"s3gimprint" inDirectory:@"Imprint Atlas"];
    if (!path) {
        [self setStatus:"ATLAS RESOURCE NOT FOUND"];
        [self setNeedsDisplay:YES];
        return;
    }
    NSData* data = [NSData dataWithContentsOfFile:path];
    s3g::AmbiImprintDescriptor descriptor;
    GuiSpaceGeometry geometry;
    std::string json;
    std::string error;
    if (!parseImprintData(data, descriptor, geometry, json, error)) {
        [self setStatus:error];
        [self setNeedsDisplay:YES];
        return;
    }
    [self setStatus:"BUILDING KERNELS"];
    [self displayIfNeeded];
    if (!installDescriptor(*_plugin, std::move(descriptor), std::move(geometry), std::move(json), entry.title, error)) [self setStatus:error];
    [self setNeedsDisplay:YES];
}

- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 634, 718, 846, 110);
}

- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = NSMakeRect(12, 34, 590, 426);
    const CGFloat width = 38.0;
    const CGFloat gap = 5.0;
    const CGFloat x = NSMaxX(panel) - 10.0 - (3.0 - static_cast<CGFloat>(index)) * width - (2.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, panel.origin.y + 4.0, width, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    const CGFloat width = 18.0;
    const CGFloat gap = 4.0;
    const CGFloat viewStart = [self viewButtonRect:0].origin.x;
    const CGFloat x = viewStart - 12.0 - (2.0 - static_cast<CGFloat>(index)) * width - (1.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, 38.0, width, 13.0);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzimuthDeg = 0.0;
        _viewElevationDeg = 0.0;
    } else if (mode == 1) {
        _viewAzimuthDeg = 0.0;
        _viewElevationDeg = 90.0;
    } else {
        _viewAzimuthDeg = 35.0;
        _viewElevationDeg = 34.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (void)drawRoom:(const GuiSnapshot&)snapshot rect:(NSRect)field attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(0x101010) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x4d4d4d) setStroke];
    NSFrameRect(field);

    std::vector<std::array<float, 2>> polygon = snapshot.polygon;
    if (polygon.size() < 3u) {
        polygon = { { 0.0f, 0.0f }, { snapshot.roomWidth, 0.0f }, { snapshot.roomWidth, snapshot.roomDepth }, { 0.0f, snapshot.roomDepth } };
    }

    float minX = polygon[0][0];
    float maxX = polygon[0][0];
    float minY = polygon[0][1];
    float maxY = polygon[0][1];
    float minZ = 0.0f;
    float maxZ = std::max(0.5f, snapshot.roomHeight);
    auto includePoint = [&](float x, float y, float z) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
        minZ = std::min(minZ, z);
        maxZ = std::max(maxZ, z);
    };
    for (const auto& point : polygon) includePoint(point[0], point[1], 0.0f);
    for (const auto& point : snapshot.ceilingProfile) includePoint(point[0], minY, point[1]);
    for (const auto& branch : snapshot.branches) {
        for (const auto& point : branch.polygon) {
            includePoint(point[0], point[1], branch.baseHeight);
            includePoint(point[0], point[1], branch.baseHeight + branch.height);
        }
    }
    for (const auto& portal : snapshot.portals) {
        for (const auto& point : portal.outline) includePoint(point[0], point[1], point[2]);
    }
    includePoint(snapshot.listenerX, snapshot.listenerY, snapshot.listenerZ);
    for (uint32_t i = 0; i < snapshot.profileCount; ++i) {
        const auto& profile = snapshot.profiles[i];
        includePoint(profile.sourceX, profile.sourceY, profile.sourceZ);
    }

    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;
    const float worldSpan = std::max({ 0.001f, maxX - minX, maxY - minY, maxZ - minZ });
    const float azimuth = static_cast<float>(_viewAzimuthDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(_viewElevationDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    auto rotatePoint = [&](float x, float y, float z) {
        const float nx = (x - centerX) / worldSpan;
        const float ny = (y - centerY) / worldSpan;
        const float nz = (z - centerZ) / worldSpan;
        const float x1 = ca * nx - sa * ny;
        const float y1 = sa * nx + ca * ny;
        const float y2 = ce * y1 + se * nz;
        const float z2 = -se * y1 + ce * nz;
        return std::array<float, 3> { x1, y2, z2 };
    };
    float projectedMinX = std::numeric_limits<float>::max();
    float projectedMaxX = std::numeric_limits<float>::lowest();
    float projectedMinY = std::numeric_limits<float>::max();
    float projectedMaxY = std::numeric_limits<float>::lowest();
    for (float x : { minX, maxX }) {
        for (float y : { minY, maxY }) {
            for (float z : { minZ, maxZ }) {
                const auto rotated = rotatePoint(x, y, z);
                projectedMinX = std::min(projectedMinX, rotated[0]);
                projectedMaxX = std::max(projectedMaxX, rotated[0]);
                projectedMinY = std::min(projectedMinY, rotated[1]);
                projectedMaxY = std::max(projectedMaxY, rotated[1]);
            }
        }
    }
    const CGFloat scale = std::min(
        (field.size.width - 42.0) / std::max(0.001f, projectedMaxX - projectedMinX),
        (field.size.height - 42.0) / std::max(0.001f, projectedMaxY - projectedMinY))
        * std::clamp(_viewZoom, 0.55, 2.20);
    auto projectPoint = [&](float x, float y, float z, CGFloat* depth = nullptr) {
        const auto rotated = rotatePoint(x, y, z);
        const float projectedCenterX = (projectedMinX + projectedMaxX) * 0.5f;
        const float projectedCenterY = (projectedMinY + projectedMaxY) * 0.5f;
        if (depth) *depth = static_cast<CGFloat>(rotated[2]);
        return NSMakePoint(NSMidX(field) + static_cast<CGFloat>(rotated[0] - projectedCenterX) * scale,
                           NSMidY(field) - static_cast<CGFloat>(rotated[1] - projectedCenterY) * scale);
    };
    auto ceilingAt = [&](float x) {
        if (snapshot.ceilingProfile.size() < 2u) return snapshot.roomHeight;
        if (x <= snapshot.ceilingProfile.front()[0]) return snapshot.ceilingProfile.front()[1];
        if (x >= snapshot.ceilingProfile.back()[0]) return snapshot.ceilingProfile.back()[1];
        for (size_t i = 1; i < snapshot.ceilingProfile.size(); ++i) {
            const auto& a = snapshot.ceilingProfile[i - 1u];
            const auto& b = snapshot.ceilingProfile[i];
            if (x > b[0]) continue;
            const float amount = (x - a[0]) / std::max(0.0001f, b[0] - a[0]);
            return a[1] + (b[1] - a[1]) * amount;
        }
        return snapshot.roomHeight;
    };

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSInsetRect(field, 1.0, 1.0));
    auto drawGeometry = [&](const std::vector<std::array<float, 2>>& points,
                            float baseHeight,
                            float fixedHeight,
                            bool variableHeight,
                            NSColor* stroke,
                            NSColor* fill) {
        if (points.size() < 3u) return;
        auto heightAt = [&](float x) { return variableHeight ? ceilingAt(x) : baseHeight + fixedHeight; };
        NSBezierPath* roof = [NSBezierPath bezierPath];
        [roof moveToPoint:projectPoint(points[0][0], points[0][1], heightAt(points[0][0]))];
        for (size_t i = 1; i < points.size(); ++i) {
            [roof lineToPoint:projectPoint(points[i][0], points[i][1], heightAt(points[i][0]))];
        }
        [roof closePath];
        [fill setFill];
        [roof fill];

        NSBezierPath* edges = [NSBezierPath bezierPath];
        const size_t verticalStride = std::max<size_t>(1u, points.size() / 8u);
        for (size_t i = 0; i < points.size(); ++i) {
            const auto& a = points[i];
            const auto& b = points[(i + 1u) % points.size()];
            [edges moveToPoint:projectPoint(a[0], a[1], baseHeight)];
            [edges lineToPoint:projectPoint(b[0], b[1], baseHeight)];
            [edges moveToPoint:projectPoint(a[0], a[1], heightAt(a[0]))];
            [edges lineToPoint:projectPoint(b[0], b[1], heightAt(b[0]))];
            if (i % verticalStride == 0u) {
                [edges moveToPoint:projectPoint(a[0], a[1], baseHeight)];
                [edges lineToPoint:projectPoint(a[0], a[1], heightAt(a[0]))];
            }
        }
        [stroke setStroke];
        [edges setLineWidth:1.1];
        [edges stroke];
    };

    drawGeometry(polygon, 0.0f, snapshot.roomHeight, true,
        s3g::clap_gui::color(0x858585, 0.78), s3g::clap_gui::color(0x747474, 0.075));
    for (const auto& branch : snapshot.branches) {
        int color = 0x66746e;
        if (branch.family == "room") color = 0x727572;
        else if (branch.family == "cavern") color = 0x60766f;
        else if (branch.family == "tunnel") color = 0x707269;
        else if (branch.family == "canyon") color = 0x767063;
        else if (branch.family == "clearing") color = 0x647665;
        else if (branch.family == "abstract") color = 0x726a78;
        drawGeometry(branch.polygon, branch.baseHeight, std::max(0.1f, branch.height), false,
            s3g::clap_gui::color(color, 0.70), s3g::clap_gui::color(color, 0.055));
    }

    for (const auto& portal : snapshot.portals) {
        if (portal.outline.size() < 3u) continue;
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:projectPoint(portal.outline[0][0], portal.outline[0][1], portal.outline[0][2])];
        for (size_t index = 1; index < portal.outline.size(); ++index) {
            [path lineToPoint:projectPoint(portal.outline[index][0], portal.outline[index][1], portal.outline[index][2])];
        }
        [path closePath];
        [s3g::clap_gui::color(0x101010, 0.72) setFill];
        [path fill];
        [s3g::clap_gui::color(0xb8dce6, 0.82) setStroke];
        const CGFloat dash[] = { 4.0, 3.0 };
        [path setLineDash:dash count:2 phase:0.0];
        [path setLineWidth:1.15];
        [path stroke];
    }

    const NSPoint listener = projectPoint(snapshot.listenerX, snapshot.listenerY, snapshot.listenerZ);
    for (uint32_t i = 0; i < snapshot.profileCount; ++i) {
        const auto& profile = snapshot.profiles[i];
        const NSPoint source = projectPoint(profile.sourceX, profile.sourceY, profile.sourceZ);
        [s3g::clap_gui::color(0x6a6a6a, 0.42) setStroke];
        [NSBezierPath strokeLineFromPoint:listener toPoint:source];
        [imprintPointColor(profile.azimuthDeg, profile.elevationDeg) setFill];
        NSRectFill(NSMakeRect(source.x - 5.0, source.y - 5.0, 10.0, 10.0));
        [s3g::clap_gui::color(0x111111, 0.8) setStroke];
        NSFrameRect(NSMakeRect(source.x - 5.0, source.y - 5.0, 10.0, 10.0));
        [[NSString stringWithFormat:@"%u", i + 1u] drawAtPoint:NSMakePoint(source.x + 8.0, source.y - 7.0) withAttributes:attrs];
    }
    [s3g::clap_gui::color(0xb0b0b0) setFill];
    NSRectFill(NSMakeRect(listener.x - 4.0, listener.y - 4.0, 8.0, 8.0));
    [s3g::clap_gui::color(0x202020) setStroke];
    NSFrameRect(NSMakeRect(listener.x - 4.0, listener.y - 4.0, 8.0, 8.0));

    if (snapshot.profileCount == 0u) {
        NSString* empty = @"LOAD AN IMPRINT .S3GIMPRINT";
        const NSSize size = [empty sizeWithAttributes:attrs];
        [empty drawAtPoint:NSMakePoint(NSMidX(field) - size.width * 0.5, NSMidY(field) - 6.0) withAttributes:attrs];
    }
    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const float peak = _plugin->outputPeak.exchange(_plugin->outputPeak.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    NSString* peakText = s3g::clap_gui::peakDbText(peak);
    NSString* info = [NSString stringWithFormat:@"%uOA / 64CH", _plugin->params.order];
    const CGFloat peakX = kGuiWidth - [peakText sizeWithAttributes:value].width - 18.0;
    const CGFloat infoX = peakX - [info sizeWithAttributes:value].width - 18.0;
    [@"s3g AMBI IMPRINT 64" drawAtPoint:NSMakePoint(18, 13) withAttributes:text];
    [info drawAtPoint:NSMakePoint(infoX, 13) withAttributes:value];
    [peakText drawAtPoint:NSMakePoint(peakX, 13) withAttributes:value];

    const NSRect fieldPanel = NSMakeRect(12, 34, 590, 426);
    const NSRect imprintPanel = NSMakeRect(616, 34, 272, 126);
    const NSRect processPanel = NSMakeRect(616, 174, 272, 170);
    const NSRect outputPanel = NSMakeRect(616, 358, 272, 102);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"IMPRINT FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i], fieldPanel, viewLabels[i], i == _viewMode, value, style);
    }
    static NSString* zoomLabels[] = { @"-", @"+" };
    for (int i = 0; i < 2; ++i) {
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i], fieldPanel, zoomLabels[i], false, value, style);
    }
    [self drawRoom:snapshot rect:NSMakeRect(28, 68, 558, 374) attrs:value];

    s3g::clap_gui::drawPanelFrame(imprintPanel.origin.x, imprintPanel.origin.y, imprintPanel.size.width, imprintPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"IMPRINT", true, imprintPanel.origin.x, imprintPanel.origin.y, imprintPanel.size.width, 21, text, style);
    s3g::clap_gui::drawHeaderActionButton(NSMakeRect(731, 36, 71, 17), NSMakeRect(616, 34, 272, 21), @"ATLAS", value, style);
    s3g::clap_gui::drawHeaderActionButton(NSMakeRect(807, 36, 68, 17), NSMakeRect(616, 34, 272, 21), @"LOAD", value, style);
    [@"FILE" drawAtPoint:NSMakePoint(628, 69) withAttributes:text];
    [compactFileName(snapshot.name) drawAtPoint:NSMakePoint(676, 69) withAttributes:value];
    [@"STAT" drawAtPoint:NSMakePoint(628, 91) withAttributes:text];
    [[NSString stringWithUTF8String:snapshot.status.c_str()] drawAtPoint:NSMakePoint(676, 91) withAttributes:value];
    [@"DIR" drawAtPoint:NSMakePoint(628, 113) withAttributes:text];
    [[NSString stringWithFormat:@"%u", snapshot.profileCount] drawAtPoint:NSMakePoint(676, 113) withAttributes:value];
    [@"LEN" drawAtPoint:NSMakePoint(738, 113) withAttributes:text];
    [[NSString stringWithFormat:@"%.2f s", snapshot.duration] drawAtPoint:NSMakePoint(778, 113) withAttributes:value];
    [@"SPACE" drawAtPoint:NSMakePoint(628, 135) withAttributes:text];
    NSString* family = [[NSString stringWithUTF8String:snapshot.family.c_str()] uppercaseString];
    [[NSString stringWithFormat:@"%@ %.1f x %.1f x %.1f", family ? family : @"ROOM", snapshot.roomWidth, snapshot.roomDepth, snapshot.roomHeight]
        drawAtPoint:NSMakePoint(676, 135) withAttributes:value];

    s3g::clap_gui::drawPanelFrame(processPanel.origin.x, processPanel.origin.y, processPanel.size.width, processPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PROCESS", true, processPanel.origin.x, processPanel.origin.y, processPanel.size.width, 21, text, style);
    s3g::clap_gui::drawMenu(@"ORD", [NSString stringWithFormat:@"%uOA", _plugin->params.order], 214, text, value, style, 634, 718, 150);
    [self drawSlider:@"MIX" value:[NSString stringWithFormat:@"%.0f%%", _plugin->params.mix * 100.0f] norm:_plugin->params.mix y:240 attrs:value style:style];
    [self drawSlider:@"FOC" value:[NSString stringWithFormat:@"%.0f%%", _plugin->params.focus * 100.0f] norm:_plugin->params.focus y:266 attrs:value style:style];
    [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.2f", _plugin->params.width] norm:_plugin->params.width / 1.5f y:292 attrs:value style:style];

    s3g::clap_gui::drawPanelFrame(outputPanel.origin.x, outputPanel.origin.y, outputPanel.size.width, outputPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, outputPanel.origin.x, outputPanel.origin.y, outputPanel.size.width, 21, text, style);
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", _plugin->params.outputGainDb] norm:(_plugin->params.outputGainDb + 60.0f) / 72.0f y:398 attrs:value style:style];
    s3g::clap_gui::drawToggle(@"BYP", _plugin->params.bypass, 424, text, value, style, 634, 718, 64);

    if (_orderMenuOpen) {
        NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(718, 230, 150, 126), 18, orderItems, 7u,
            static_cast<int>(_plugin->params.order) - 1, _orderMenuHover, value, style);
    }
    if (_atlasMenuOpen) {
        NSString* atlasItems[kImprintAtlas.size()];
        for (size_t i = 0; i < kImprintAtlas.size(); ++i) atlasItems[i] = [NSString stringWithUTF8String:kImprintAtlas[i].title];
        s3g::clap_gui::drawDropdownMenu(imprintAtlasMenuRect(), 18, atlasItems,
            static_cast<uint32_t>(kImprintAtlas.size()), -1, _atlasMenuHover, value, style);
    }
}

- (void)resetParam:(clap_id)param
{
    switch (param) {
    case kParamMix: [self setParam:param value:0.5]; break;
    case kParamFocus: [self setParam:param value:1.0]; break;
    case kParamWidth: [self setParam:param value:1.0]; break;
    case kParamOutput: [self setParam:param value:0.0]; break;
    default: break;
    }
}

- (void)updateSliderAtPoint:(NSPoint)point
{
    const double norm = std::clamp((point.x - 718.0) / 110.0, 0.0, 1.0);
    switch (_dragParam) {
    case kParamMix: [self setParam:kParamMix value:norm]; break;
    case kParamFocus: [self setParam:kParamFocus value:norm]; break;
    case kParamWidth: [self setParam:kParamWidth value:norm * 1.5]; break;
    case kParamOutput: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    default: break;
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_atlasMenuOpen) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, imprintAtlasMenuRect(), 18,
            static_cast<uint32_t>(kImprintAtlas.size()));
        _atlasMenuOpen = false;
        _atlasMenuHover = -1;
        if (hit >= 0) [self loadAtlasAtIndex:static_cast<NSUInteger>(hit)];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_orderMenuOpen) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, NSMakeRect(718, 230, 150, 126), 18, 7u);
        _orderMenuOpen = false;
        _orderMenuHover = -1;
        if (hit >= 0) [self setParam:kParamOrder value:hit + 1];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(807, 36, 68, 17))) {
        [self loadImprint];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(731, 36, 71, 17))) {
        _atlasMenuOpen = true;
        _atlasMenuHover = -1;
        _orderMenuOpen = false;
        [self setNeedsDisplay:YES];
        return;
    }
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(point, [self viewButtonRect:i])) {
            [self setViewPreset:i];
            return;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (NSPointInRect(point, [self zoomButtonRect:i])) {
            _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.86 : 1.16), 0.55, 2.20);
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, NSMakeRect(718, 213, 150, 18))) {
        _orderMenuOpen = true;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(718, 423, 64, 18))) {
        [self setParam:kParamBypass value:_plugin->params.bypass ? 0.0 : 1.0];
        return;
    }
    struct Row { clap_id param; CGFloat y; };
    const Row rows[] = { { kParamMix, 240 }, { kParamFocus, 266 }, { kParamWidth, 292 }, { kParamOutput, 398 } };
    for (const auto& row : rows) {
        if (!NSPointInRect(point, NSMakeRect(624, row.y - 8, 254, 24))) continue;
        if ([event clickCount] >= 2) {
            [self resetParam:row.param];
            return;
        }
        _dragParam = row.param;
        [self updateSliderAtPoint:point];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(28, 68, 558, 374))) {
        _dragView = YES;
        _lastDragPoint = point;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        _viewAzimuthDeg += (point.x - _lastDragPoint.x) * 0.62;
        _viewElevationDeg = std::clamp(_viewElevationDeg + (point.y - _lastDragPoint.y) * 0.52, -89.0, 89.0);
        while (_viewAzimuthDeg > 180.0) _viewAzimuthDeg -= 360.0;
        while (_viewAzimuthDeg < -180.0) _viewAzimuthDeg += 360.0;
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam != CLAP_INVALID_ID) [self updateSliderAtPoint:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragView = NO;
    [self storeViewState];
}

- (void)mouseMoved:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_atlasMenuOpen) {
        _atlasMenuHover = s3g::clap_gui::dropdownHitIndex(point, imprintAtlasMenuRect(), 18,
            static_cast<uint32_t>(kImprintAtlas.size()));
    } else if (_orderMenuOpen) {
        _orderMenuHover = s3g::clap_gui::dropdownHitIndex(point, NSMakeRect(718, 230, 150, 126), 18, 7u);
    } else {
        return;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_orderMenuHover != -1) {
        _orderMenuHover = -1;
        [self setNeedsDisplay:YES];
    }
    if (_atlasMenuHover != -1) {
        _atlasMenuHover = -1;
        [self setNeedsDisplay:YES];
    }
}

- (void)scrollWheel:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (!NSPointInRect(point, NSMakeRect(28, 68, 558, 374))) {
        [super scrollWheel:event];
        return;
    }
    _viewZoom = std::clamp(_viewZoom * std::exp(-[event scrollingDeltaY] * 0.035), 0.55, 2.20);
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GAmbiImprintView alloc] initWithPlugin:instance];
    return instance->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return;
    instance->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GAmbiImprintView*>(instance->guiView);
    [view stopRefreshTimer];
    [view removeFromSuperview];
    [view release];
    instance->guiView = nullptr;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height) { if (!width || !height) return false; *width = kGuiWidth; *height = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = false; hints->can_resize_vertically = false; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* width, uint32_t* height) { if (!width || !height) return false; *width = kGuiWidth; *height = kGuiHeight; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height) { auto* instance = self(plugin); if (!instance->guiView) return false; [static_cast<NSView*>(instance->guiView) setFrameSize:NSMakeSize(width, height)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window) { if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false; auto* instance = self(plugin); if (!instance->guiView) return false; NSView* parent = static_cast<NSView*>(window->cocoa); NSView* view = static_cast<NSView*>(instance->guiView); [parent addSubview:view]; [view setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* instance = self(plugin); if (!instance->guiView) return false; instance->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(instance->guiView) setHidden:NO]; [static_cast<S3GAmbiImprintView*>(instance->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* instance = self(plugin); if (!instance->guiView) return false; instance->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbiImprintView*>(instance->guiView) stopRefreshTimer]; [static_cast<NSView*>(instance->guiView) setHidden:YES]; return true; }

const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, CLAP_PLUGIN_FEATURE_REVERB, nullptr };

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kPluginName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    kPluginDesc,
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->hostTail = host && host->get_extension ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL)) : nullptr;
    instance->params = s3g::sanitizeAmbiImprintParams(instance->params);
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0u ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
