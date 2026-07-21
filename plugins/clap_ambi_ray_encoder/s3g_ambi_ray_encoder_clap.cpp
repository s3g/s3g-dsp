#include "s3g_ambi_ray_encoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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

constexpr uint32_t kInputChannels = 1u;
constexpr uint32_t kOutputChannels = 64u;
constexpr uint32_t kGuiWidth = 1040u;
constexpr uint32_t kGuiHeight = 620u;
constexpr uint32_t kStateMagic = 0x53475259u;
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kMaximumStateJsonBytes = 8u * 1024u * 1024u;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-ray-encoder";
constexpr const char* kPluginName = "s3g Ambi Ray Encoder";
constexpr const char* kPluginDesc = "Moving mono source encoder with interpolated ray-field room response.";

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamSourceX = 2,
    kParamSourceY = 3,
    kParamSourceZ = 4,
    kParamDirect = 5,
    kParamEarly = 6,
    kParamLate = 7,
    kParamSize = 8,
    kParamScatter = 9,
    kParamWidth = 10,
    kParamAir = 11,
    kParamMovement = 12,
    kParamOutput = 13,
    kParamBypass = 14,
};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    s3g::AmbiRayEncoderParams params {};
    uint32_t jsonBytes = 0u;
};

struct GuiBranchGeometry {
    std::string family = "room";
    std::string attachment = "wall";
    float baseHeight = 0.0f;
    float height = 3.0f;
    std::vector<std::array<float, 2>> polygon;
};

struct GuiPortalGeometry {
    std::array<float, 3> center {};
    float width = 1.0f;
    float height = 2.0f;
};

struct GuiSpaceGeometry {
    std::string family = "room";
    std::vector<GuiBranchGeometry> branches;
    std::vector<GuiPortalGeometry> portals;
};

struct GuiReflectionSnapshot {
    float delayMs = 0.0f;
    float gain = 0.0f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
};

struct GuiSnapshot {
    std::string name;
    std::string status;
    std::string family;
    s3g::AmbiRayRoom room {};
    s3g::Vec3 listener {};
    s3g::Vec3 source {};
    std::vector<s3g::Vec3> cells;
    std::vector<GuiBranchGeometry> branches;
    std::vector<GuiPortalGeometry> portals;
    std::vector<GuiReflectionSnapshot> reflections;
    uint32_t nearestCell = 0u;
    float nearestDistance = 0.0f;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    s3g::AmbiRayEncoderParams params {};
    s3g::AmbiRayDescriptor descriptor = s3g::makeDefaultAmbiRayDescriptor();
    GuiSpaceGeometry guiGeometry {};
    std::string rayJson;
    std::string rayName = "BUILT-IN ROOM";
    std::string status = "READY";
    std::mutex stateMutex;
    std::vector<std::unique_ptr<s3g::AmbiRayEncoder>> runtimes;
    std::atomic<s3g::AmbiRayEncoder*> activeProcessor { nullptr };
    std::atomic<bool> active { false };
    std::atomic<float> outputPeak { 0.0f };
    double sampleRate = 48000.0;
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
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

s3g::Vec3 sourcePosition(const s3g::AmbiRayDescriptor& descriptor, const s3g::AmbiRayEncoderParams& params)
{
    const auto& minimum = descriptor.room.navigationMinimumMetres;
    const auto& maximum = descriptor.room.navigationMaximumMetres;
    return {
        s3g::lerp(minimum.x, maximum.x, params.sourceX),
        s3g::lerp(minimum.y, maximum.y, params.sourceY),
        s3g::lerp(minimum.z, maximum.z, params.sourceZ)
    };
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kParamOrder: plugin.params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kParamSourceX: plugin.params.sourceX = static_cast<float>(value); break;
    case kParamSourceY: plugin.params.sourceY = static_cast<float>(value); break;
    case kParamSourceZ: plugin.params.sourceZ = static_cast<float>(value); break;
    case kParamDirect: plugin.params.direct = static_cast<float>(value); break;
    case kParamEarly: plugin.params.early = static_cast<float>(value); break;
    case kParamLate: plugin.params.late = static_cast<float>(value); break;
    case kParamSize: plugin.params.size = static_cast<float>(value); break;
    case kParamScatter: plugin.params.scatter = static_cast<float>(value); break;
    case kParamWidth: plugin.params.width = static_cast<float>(value); break;
    case kParamAir: plugin.params.air = static_cast<float>(value); break;
    case kParamMovement: plugin.params.movementMs = static_cast<float>(value); break;
    case kParamOutput: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kParamBypass: plugin.params.bypassRoom = value >= 0.5; break;
    default: return;
    }
    plugin.params = s3g::sanitizeAmbiRayEncoderParams(plugin.params);
    if (auto* processor = plugin.activeProcessor.load(std::memory_order_acquire)) processor->setParams(plugin.params);
}

double getParam(const Plugin& plugin, clap_id id)
{
    switch (id) {
    case kParamOrder: return plugin.params.order;
    case kParamSourceX: return plugin.params.sourceX;
    case kParamSourceY: return plugin.params.sourceY;
    case kParamSourceZ: return plugin.params.sourceZ;
    case kParamDirect: return plugin.params.direct;
    case kParamEarly: return plugin.params.early;
    case kParamLate: return plugin.params.late;
    case kParamSize: return plugin.params.size;
    case kParamScatter: return plugin.params.scatter;
    case kParamWidth: return plugin.params.width;
    case kParamAir: return plugin.params.air;
    case kParamMovement: return plugin.params.movementMs;
    case kParamOutput: return plugin.params.outputGainDb;
    case kParamBypass: return plugin.params.bypassRoom ? 1.0 : 0.0;
    default: return 0.0;
    }
}

bool buildRuntime(Plugin& plugin, const s3g::AmbiRayDescriptor& descriptor, std::string& error)
{
    auto runtime = std::make_unique<s3g::AmbiRayEncoder>();
    runtime->setParams(plugin.params);
    if (!runtime->prepare(plugin.sampleRate, descriptor)) {
        error = "RAY FIELD BUILD FAILED";
        return false;
    }
    auto* next = runtime.get();
    plugin.runtimes.push_back(std::move(runtime));
    plugin.activeProcessor.store(next, std::memory_order_release);
    return true;
}

bool installDescriptor(Plugin& plugin,
                       s3g::AmbiRayDescriptor descriptor,
                       GuiSpaceGeometry geometry,
                       std::string json,
                       std::string name,
                       bool adoptDefaultSource,
                       std::string& error)
{
    descriptor = s3g::sanitizeAmbiRayDescriptor(std::move(descriptor));
    if (descriptor.cells.empty()) {
        error = "RAY FIELD HAS NO CELLS";
        return false;
    }
    if (adoptDefaultSource) {
        const s3g::Vec3 normalized = s3g::AmbiRayEncoder::normalizedSourcePosition(
            descriptor, descriptor.defaultSourcePositionMetres);
        plugin.params.sourceX = normalized.x;
        plugin.params.sourceY = normalized.y;
        plugin.params.sourceZ = normalized.z;
        plugin.params = s3g::sanitizeAmbiRayEncoderParams(plugin.params);
    }
    if (plugin.active.load(std::memory_order_acquire) && !buildRuntime(plugin, descriptor, error)) return false;
    {
        std::lock_guard<std::mutex> lock(plugin.stateMutex);
        plugin.descriptor = std::move(descriptor);
        plugin.guiGeometry = std::move(geometry);
        plugin.rayJson = std::move(json);
        plugin.rayName = std::move(name);
        plugin.status = "READY";
    }
    if (plugin.hostTail && plugin.host) plugin.hostTail->changed(plugin.host);
    return true;
}

GuiSnapshot guiSnapshot(Plugin& plugin)
{
    GuiSnapshot result;
    s3g::AmbiRayEncoderParams params;
    {
        std::lock_guard<std::mutex> lock(plugin.stateMutex);
        result.name = plugin.rayName;
        result.status = plugin.status;
        result.family = plugin.guiGeometry.family;
        result.room = plugin.descriptor.room;
        result.listener = plugin.descriptor.listenerPositionMetres;
        result.branches = plugin.guiGeometry.branches;
        result.portals = plugin.guiGeometry.portals;
        params = plugin.params;
        result.source = sourcePosition(plugin.descriptor, params);
        result.cells.reserve(plugin.descriptor.cells.size());
        float nearest = std::numeric_limits<float>::max();
        for (uint32_t index = 0u; index < plugin.descriptor.cells.size(); ++index) {
            const auto& cell = plugin.descriptor.cells[index];
            result.cells.push_back(cell.positionMetres);
            const float dx = result.source.x - cell.positionMetres.x;
            const float dy = result.source.y - cell.positionMetres.y;
            const float dz = result.source.z - cell.positionMetres.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance < nearest) {
                nearest = distance;
                result.nearestCell = index;
            }
        }
        result.nearestDistance = std::isfinite(nearest) ? nearest : 0.0f;
        if (result.nearestCell < plugin.descriptor.cells.size()) {
            const auto& cell = plugin.descriptor.cells[result.nearestCell];
            const uint32_t count = std::min<uint32_t>(12u, static_cast<uint32_t>(cell.reflections.size()));
            result.reflections.reserve(count);
            for (uint32_t index = 0u; index < count; ++index) {
                const auto& event = cell.reflections[index];
                result.reflections.push_back({ event.delayMs, event.gain, event.azimuthDeg, event.elevationDeg });
            }
        }
    }
    if (result.room.polygon.size() < 3u) {
        result.room.polygon = { { 0.0f, 0.0f }, { result.room.widthMetres, 0.0f },
            { result.room.widthMetres, result.room.depthMetres }, { 0.0f, result.room.depthMetres } };
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

s3g::Vec3 parseVector3(NSDictionary* value, s3g::Vec3 fallback = {})
{
    if (![value isKindOfClass:[NSDictionary class]]) return fallback;
    return { numberValue(value, @"x", fallback.x), numberValue(value, @"y", fallback.y), numberValue(value, @"z", fallback.z) };
}

std::vector<std::array<float, 2>> parsePointPairs(NSArray* values, NSString* secondKey, uint32_t maximum = 64u)
{
    std::vector<std::array<float, 2>> result;
    if (![values isKindOfClass:[NSArray class]]) return result;
    const uint32_t count = std::min<uint32_t>(maximum, static_cast<uint32_t>([values count]));
    result.reserve(count);
    for (uint32_t index = 0u; index < count; ++index) {
        id item = [values objectAtIndex:index];
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        auto* point = static_cast<NSDictionary*>(item);
        result.push_back({ numberValue(point, @"x", 0.0f), numberValue(point, secondKey, 0.0f) });
    }
    return result;
}

bool parseRayData(NSData* data,
                  s3g::AmbiRayDescriptor& descriptor,
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
    auto* root = static_cast<NSDictionary*>(rootObject);
    NSString* format = [root objectForKey:@"format"];
    if (![format isKindOfClass:[NSString class]] || ![format isEqualToString:@"s3g-ambi-ray-field"]) {
        error = "NOT AN S3G RAY FIELD";
        return false;
    }
    if (unsignedValue(root, @"version", 0u) != s3g::kAmbiRayFormatVersion) {
        error = "UNSUPPORTED FORMAT VERSION";
        return false;
    }
    NSArray* cells = arrayValue(root, @"cells");
    if (!cells || [cells count] == 0u || [cells count] > s3g::kAmbiRayMaxCells) {
        error = "EXPECTED 1-256 RAY CELLS";
        return false;
    }

    s3g::AmbiRayDescriptor parsed;
    GuiSpaceGeometry parsedGeometry;
    parsed.durationSeconds = numberValue(root, @"duration_s", 3.0f);
    std::vector<std::array<float, 2>> spacePolygon;
    if (NSDictionary* space = dictionaryValue(root, @"space")) {
        parsedGeometry.family = stringValue(space, @"family", "room");
        spacePolygon = parsePointPairs(arrayValue(space, @"primary_polygon_xy_m"), @"y");
        if (NSArray* regions = arrayValue(space, @"regions")) {
            const uint32_t count = std::min<uint32_t>(64u, static_cast<uint32_t>([regions count]));
            for (uint32_t index = 0u; index < count; ++index) {
                id item = [regions objectAtIndex:index];
                if (![item isKindOfClass:[NSDictionary class]]) continue;
                auto* region = static_cast<NSDictionary*>(item);
                if (stringValue(region, @"kind", "branch") == "primary") continue;
                GuiBranchGeometry branch;
                branch.family = stringValue(region, @"family", parsedGeometry.family.c_str());
                branch.attachment = stringValue(region, @"attachment", "wall");
                branch.baseHeight = numberValue(region, @"base_z_m", 0.0f);
                branch.height = numberValue(region, @"height_m", 3.0f);
                branch.polygon = parsePointPairs(arrayValue(region, @"polygon_xy_m"), @"y");
                if (branch.polygon.size() >= 3u) parsedGeometry.branches.push_back(std::move(branch));
            }
        }
        if (NSArray* portals = arrayValue(space, @"portals")) {
            const uint32_t count = std::min<uint32_t>(64u, static_cast<uint32_t>([portals count]));
            for (uint32_t index = 0u; index < count; ++index) {
                id item = [portals objectAtIndex:index];
                if (![item isKindOfClass:[NSDictionary class]]) continue;
                auto* portalObject = static_cast<NSDictionary*>(item);
                GuiPortalGeometry portal;
                const auto center = parseVector3(dictionaryValue(portalObject, @"center_m"));
                portal.center = { center.x, center.y, center.z };
                portal.width = numberValue(portalObject, @"width_m", 1.0f);
                portal.height = numberValue(portalObject, @"height_m", 2.0f);
                parsedGeometry.portals.push_back(portal);
            }
        }
    }
    if (NSDictionary* room = dictionaryValue(root, @"room")) {
        parsedGeometry.family = stringValue(room, @"family", parsedGeometry.family.c_str());
        if (NSDictionary* dimensions = dictionaryValue(room, @"dimensions_m")) {
            parsed.room.widthMetres = numberValue(dimensions, @"x", 8.0f);
            parsed.room.depthMetres = numberValue(dimensions, @"y", 10.0f);
            parsed.room.heightMetres = numberValue(dimensions, @"z", 3.0f);
        }
        parsed.room.polygon = parsePointPairs(arrayValue(room, @"polygon_xy_m"), @"y");
        parsed.room.ceilingProfile = parsePointPairs(arrayValue(room, @"ceiling_profile_xz_m"), @"z");
        if (NSDictionary* bounds = dictionaryValue(room, @"navigation_bounds_m")) {
            parsed.room.navigationMinimumMetres = parseVector3(dictionaryValue(bounds, @"minimum"), { 0.0f, 0.0f, 0.0f });
            parsed.room.navigationMaximumMetres = parseVector3(dictionaryValue(bounds, @"maximum"),
                { parsed.room.widthMetres, parsed.room.depthMetres, parsed.room.heightMetres });
        } else {
            parsed.room.navigationMinimumMetres = { 0.0f, 0.0f, 0.0f };
            parsed.room.navigationMaximumMetres = { parsed.room.widthMetres, parsed.room.depthMetres, parsed.room.heightMetres };
        }
    }
    if (parsed.room.polygon.size() < 3u && spacePolygon.size() >= 3u) parsed.room.polygon = std::move(spacePolygon);
    if (parsed.room.ceilingProfile.size() < 2u) {
        parsed.room.ceilingProfile = { { 0.0f, parsed.room.heightMetres }, { parsed.room.widthMetres, parsed.room.heightMetres } };
    }
    parsed.listenerPositionMetres = parseVector3(dictionaryValue(root, @"listener_position_m"),
        { parsed.room.widthMetres * 0.5f, parsed.room.depthMetres * 0.5f, parsed.room.heightMetres * 0.5f });
    parsed.defaultSourcePositionMetres = parseVector3(dictionaryValue(root, @"default_source_position_m"),
        { parsed.room.widthMetres * 0.5f, parsed.room.depthMetres * 0.25f, parsed.room.heightMetres * 0.5f });

    const uint32_t cellCount = static_cast<uint32_t>([cells count]);
    parsed.cells.reserve(cellCount);
    for (uint32_t cellIndex = 0u; cellIndex < cellCount; ++cellIndex) {
        id item = [cells objectAtIndex:cellIndex];
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        auto* source = static_cast<NSDictionary*>(item);
        s3g::AmbiRayCell cell;
        cell.positionMetres = parseVector3(dictionaryValue(source, @"position_m"), parsed.defaultSourcePositionMetres);
        NSArray* reflections = arrayValue(source, @"early_reflections");
        if (!reflections) reflections = arrayValue(source, @"reflections");
        const uint32_t reflectionCount = std::min<uint32_t>(s3g::kAmbiRayMaxReflections,
            reflections ? static_cast<uint32_t>([reflections count]) : 0u);
        cell.reflections.reserve(reflectionCount);
        for (uint32_t reflectionIndex = 0u; reflectionIndex < reflectionCount; ++reflectionIndex) {
            id reflectionItem = [reflections objectAtIndex:reflectionIndex];
            if (![reflectionItem isKindOfClass:[NSDictionary class]]) continue;
            auto* reflection = static_cast<NSDictionary*>(reflectionItem);
            cell.reflections.push_back({
                unsignedValue(reflection, @"slot", reflectionIndex),
                numberValue(reflection, @"delay_ms", 20.0f),
                numberValue(reflection, @"gain", 0.0f),
                numberValue(reflection, @"azimuth_deg", 0.0f),
                numberValue(reflection, @"elevation_deg", 0.0f),
                numberValue(reflection, @"damping", 0.25f)
            });
        }
        if (NSDictionary* late = dictionaryValue(source, @"late")) {
            cell.late.startMs = numberValue(late, @"start_ms", 45.0f);
            cell.late.decaySeconds = numberValue(late, @"decay_s", 1.8f);
            cell.late.level = numberValue(late, @"level", 0.18f);
            cell.late.diffusion = numberValue(late, @"diffusion", 0.72f);
            cell.late.damping = numberValue(late, @"damping", 0.38f);
        }
        parsed.cells.push_back(std::move(cell));
    }
    if (parsed.cells.empty()) {
        error = "NO VALID RAY CELLS";
        return false;
    }
    descriptor = s3g::sanitizeAmbiRayDescriptor(std::move(parsed));
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
    s3g::AmbiRayDescriptor descriptor;
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        descriptor = instance->descriptor;
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
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = inputEvents->get(inputEvents, index);
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
    if (!processor || !outputData) return CLAP_PROCESS_CONTINUE;
    processor->setParams(plugin.params);
    const Sample* mono = inputData && input.channel_count > 0u ? inputData[0] : nullptr;
    processor->process(mono, outputData, output.channel_count, frames);
    float peak = 0.0f;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (!outputData[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame)
            peak = std::max(peak, static_cast<float>(std::abs(outputData[channel][frame])));
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
    std::strncpy(info->name, isInput ? "Ray Source In" : "Ambisonic Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_MONO : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 14u; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Ambi Ray Encoder", sizeof(info->module));
    switch (index) {
    case 0: info->id = kParamOrder; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Order", sizeof(info->name)); info->min_value = 1; info->max_value = 7; info->default_value = 3; return true;
    case 1: info->id = kParamSourceX; std::strncpy(info->name, "Source X", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.5; return true;
    case 2: info->id = kParamSourceY; std::strncpy(info->name, "Source Y", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.25; return true;
    case 3: info->id = kParamSourceZ; std::strncpy(info->name, "Source Z", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.5; return true;
    case 4: info->id = kParamDirect; std::strncpy(info->name, "Direct", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; return true;
    case 5: info->id = kParamEarly; std::strncpy(info->name, "Early", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 0.72; return true;
    case 6: info->id = kParamLate; std::strncpy(info->name, "Late", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 0.42; return true;
    case 7: info->id = kParamSize; std::strncpy(info->name, "Room size", sizeof(info->name)); info->min_value = 0.5; info->max_value = 2; info->default_value = 1; return true;
    case 8: info->id = kParamScatter; std::strncpy(info->name, "Scatter", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.45; return true;
    case 9: info->id = kParamWidth; std::strncpy(info->name, "Width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; return true;
    case 10: info->id = kParamAir; std::strncpy(info->name, "Air", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.2; return true;
    case 11: info->id = kParamMovement; std::strncpy(info->name, "Movement smoothing", sizeof(info->name)); info->min_value = 10; info->max_value = 500; info->default_value = 60; return true;
    case 12: info->id = kParamOutput; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = -6; return true;
    case 13: info->id = kParamBypass; info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS; std::strncpy(info->name, "Bypass room", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0; return true;
    default: return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value || paramId < kParamOrder || paramId > kParamBypass) return false;
    *value = getParam(*self(plugin), paramId);
    return true;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    const auto* instance = self(plugin);
    switch (paramId) {
    case kParamOrder: std::snprintf(display, size, "%.0fOA", value); return true;
    case kParamSourceX: {
        const auto& room = instance->descriptor.room;
        std::snprintf(display, size, "%.2f m", s3g::lerp(room.navigationMinimumMetres.x, room.navigationMaximumMetres.x, static_cast<float>(value)));
        return true;
    }
    case kParamSourceY: {
        const auto& room = instance->descriptor.room;
        std::snprintf(display, size, "%.2f m", s3g::lerp(room.navigationMinimumMetres.y, room.navigationMaximumMetres.y, static_cast<float>(value)));
        return true;
    }
    case kParamSourceZ: {
        const auto& room = instance->descriptor.room;
        std::snprintf(display, size, "%.2f m", s3g::lerp(room.navigationMinimumMetres.z, room.navigationMaximumMetres.z, static_cast<float>(value)));
        return true;
    }
    case kParamDirect:
    case kParamEarly:
    case kParamLate: std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    case kParamSize:
    case kParamWidth: std::snprintf(display, size, "%.2f", value); return true;
    case kParamScatter:
    case kParamAir: std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    case kParamMovement: std::snprintf(display, size, "%.0f ms", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    case kParamBypass: std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off"); return true;
    default: return false;
    }
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id paramId, const char* display, double* value)
{
    if (!display || !value || paramId < kParamOrder || paramId > kParamBypass) return false;
    const double parsed = std::atof(display);
    const auto* instance = self(plugin);
    switch (paramId) {
    case kParamSourceX: {
        const auto& room = instance->descriptor.room;
        *value = (parsed - room.navigationMinimumMetres.x) / std::max(0.0001f, room.navigationMaximumMetres.x - room.navigationMinimumMetres.x);
        break;
    }
    case kParamSourceY: {
        const auto& room = instance->descriptor.room;
        *value = (parsed - room.navigationMinimumMetres.y) / std::max(0.0001f, room.navigationMaximumMetres.y - room.navigationMinimumMetres.y);
        break;
    }
    case kParamSourceZ: {
        const auto& room = instance->descriptor.room;
        *value = (parsed - room.navigationMinimumMetres.z) / std::max(0.0001f, room.navigationMaximumMetres.z - room.navigationMinimumMetres.z);
        break;
    }
    case kParamDirect:
    case kParamEarly:
    case kParamLate:
    case kParamScatter:
    case kParamAir: *value = parsed * 0.01; break;
    case kParamBypass:
        *value = (display[0] == 'O' || display[0] == 'o')
            && (display[1] == 'N' || display[1] == 'n') ? 1.0 : 0.0;
        break;
    default: *value = parsed; break;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* instance = self(plugin);
    std::string json;
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        json = instance->rayJson;
    }
    if (json.size() > kMaximumStateJsonBytes) return false;
    const SavedStateHeader header { kStateMagic, kStateVersion, instance->params, static_cast<uint32_t>(json.size()) };
    return streamWriteAll(stream, &header, sizeof(header))
        && (json.empty() || streamWriteAll(stream, json.data(), json.size()));
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
    instance->params = s3g::sanitizeAmbiRayEncoderParams(header.params);
    if (header.jsonBytes > 0u) {
        std::string json(header.jsonBytes, '\0');
        if (!streamReadAll(stream, json.data(), json.size())) return false;
#if defined(__APPLE__)
        NSData* data = [NSData dataWithBytes:json.data() length:json.size()];
        s3g::AmbiRayDescriptor descriptor;
        GuiSpaceGeometry geometry;
        std::string canonical;
        std::string error;
        if (!parseRayData(data, descriptor, geometry, canonical, error)) return false;
        if (!installDescriptor(*instance, std::move(descriptor), std::move(geometry), std::move(canonical), "PROJECT STATE", false, error)) return false;
#else
        return false;
#endif
    }
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) processor->setParams(instance->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->params.bypassRoom) return 0u;
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) return processor->tailFrames();
    return 0u;
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
namespace {

NSString* compactFileName(const std::string& name)
{
    NSString* value = [NSString stringWithUTF8String:name.c_str()];
    if (!value) return @"-";
    if ([value length] <= 27u) return value;
    return [NSString stringWithFormat:@"...%@", [value substringFromIndex:[value length] - 24u]];
}

struct GuiWorldBounds {
    float minimumX = 0.0f;
    float maximumX = 8.0f;
    float minimumY = 0.0f;
    float maximumY = 10.0f;
    float minimumZ = 0.0f;
    float maximumZ = 3.0f;
};

GuiWorldBounds guiWorldBounds(const GuiSnapshot& snapshot)
{
    GuiWorldBounds bounds;
    bounds.minimumX = snapshot.room.navigationMinimumMetres.x;
    bounds.maximumX = snapshot.room.navigationMaximumMetres.x;
    bounds.minimumY = snapshot.room.navigationMinimumMetres.y;
    bounds.maximumY = snapshot.room.navigationMaximumMetres.y;
    bounds.minimumZ = std::min(0.0f, snapshot.room.navigationMinimumMetres.z);
    bounds.maximumZ = std::max(snapshot.room.heightMetres, snapshot.room.navigationMaximumMetres.z);
    auto include = [&](float x, float y, float z) {
        bounds.minimumX = std::min(bounds.minimumX, x);
        bounds.maximumX = std::max(bounds.maximumX, x);
        bounds.minimumY = std::min(bounds.minimumY, y);
        bounds.maximumY = std::max(bounds.maximumY, y);
        bounds.minimumZ = std::min(bounds.minimumZ, z);
        bounds.maximumZ = std::max(bounds.maximumZ, z);
    };
    for (const auto& point : snapshot.room.polygon) include(point[0], point[1], 0.0f);
    for (const auto& point : snapshot.room.ceilingProfile) include(point[0], 0.0f, point[1]);
    for (const auto& branch : snapshot.branches) {
        for (const auto& point : branch.polygon) {
            include(point[0], point[1], branch.baseHeight);
            include(point[0], point[1], branch.baseHeight + branch.height);
        }
    }
    include(snapshot.listener.x, snapshot.listener.y, snapshot.listener.z);
    include(snapshot.source.x, snapshot.source.y, snapshot.source.z);
    for (const auto& cell : snapshot.cells) include(cell.x, cell.y, cell.z);
    if (bounds.maximumX < bounds.minimumX + 0.1f) bounds.maximumX = bounds.minimumX + 0.1f;
    if (bounds.maximumY < bounds.minimumY + 0.1f) bounds.maximumY = bounds.minimumY + 0.1f;
    if (bounds.maximumZ < bounds.minimumZ + 0.1f) bounds.maximumZ = bounds.minimumZ + 0.1f;
    const float padX = (bounds.maximumX - bounds.minimumX) * 0.05f;
    const float padY = (bounds.maximumY - bounds.minimumY) * 0.05f;
    const float padZ = (bounds.maximumZ - bounds.minimumZ) * 0.07f;
    bounds.minimumX -= padX;
    bounds.maximumX += padX;
    bounds.minimumY -= padY;
    bounds.maximumY += padY;
    bounds.minimumZ -= padZ;
    bounds.maximumZ += padZ;
    return bounds;
}

NSRect topFieldRect() { return NSMakeRect(24, 68, 330, 452); }
NSRect sideFieldRect() { return NSMakeRect(366, 68, 330, 452); }
NSRect fieldPlotRect(NSRect field) { return NSMakeRect(field.origin.x + 15, field.origin.y + 29, field.size.width - 30, field.size.height - 44); }
NSRect orderMenuRect() { return NSMakeRect(830, 414, 82, 126); }

} // namespace

@interface S3GAmbiRayEncoderView : NSView {
    Plugin* _plugin;
    clap_id _dragParam;
    NSInteger _sourceDragView;
    NSTimer* _refreshTimer;
    bool _orderMenuOpen;
    int _orderMenuHover;
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)loadRayField;
- (void)updateSliderAtPoint:(NSPoint)point;
- (void)updateSourceAtPoint:(NSPoint)point view:(NSInteger)view;
@end

@implementation S3GAmbiRayEncoderView

- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _sourceDragView = 0;
        _refreshTimer = nil;
        _orderMenuOpen = false;
        _orderMenuHover = -1;
    }
    return self;
}

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) [self removeTrackingArea:area];
    NSTrackingArea* area = [[[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow
        owner:self userInfo:nil] autorelease];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 30.0) target:self selector:@selector(refreshTimerFired:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_refreshTimer) return;
    [_refreshTimer invalidate];
    _refreshTimer = nil;
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

- (void)loadRayField
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"s3gray"]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    NSData* data = [NSData dataWithContentsOfURL:[panel URL]];
    s3g::AmbiRayDescriptor descriptor;
    GuiSpaceGeometry geometry;
    std::string json;
    std::string error;
    if (!parseRayData(data, descriptor, geometry, json, error)) {
        [self setStatus:error];
        [self setNeedsDisplay:YES];
        return;
    }
    NSString* lastPath = [[panel URL] lastPathComponent];
    const std::string name = lastPath ? std::string([lastPath UTF8String]) : std::string("RAY FIELD");
    [self setStatus:"BUILDING FIELD"];
    [self displayIfNeeded];
    if (!installDescriptor(*_plugin, std::move(descriptor), std::move(geometry), std::move(json), name, true, error))
        [self setStatus:error];
    [self setNeedsDisplay:YES];
}

- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 742, 830, 990, 145);
}

- (void)drawField:(const GuiSnapshot&)snapshot rect:(NSRect)field side:(BOOL)side attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(0x0d1011) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x4b5052) setStroke];
    NSFrameRect(field);
    NSString* label = side ? @"SIDE  X / Z" : @"TOP  X / Y";
    [label drawAtPoint:NSMakePoint(field.origin.x + 9, field.origin.y + 8) withAttributes:attrs];

    const GuiWorldBounds bounds = guiWorldBounds(snapshot);
    const NSRect plot = fieldPlotRect(field);
    auto project = [&](float x, float y, float z) {
        const float nx = (x - bounds.minimumX) / std::max(0.001f, bounds.maximumX - bounds.minimumX);
        const float vertical = side
            ? (z - bounds.minimumZ) / std::max(0.001f, bounds.maximumZ - bounds.minimumZ)
            : (y - bounds.minimumY) / std::max(0.001f, bounds.maximumY - bounds.minimumY);
        return NSMakePoint(plot.origin.x + nx * plot.size.width,
            plot.origin.y + (1.0f - vertical) * plot.size.height);
    };

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSInsetRect(plot, -1.0, -1.0));
    [s3g::clap_gui::color(0x24292b) setStroke];
    for (int index = 1; index < 5; ++index) {
        const CGFloat x = plot.origin.x + plot.size.width * static_cast<CGFloat>(index) / 5.0;
        const CGFloat y = plot.origin.y + plot.size.height * static_cast<CGFloat>(index) / 5.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, plot.origin.y) toPoint:NSMakePoint(x, NSMaxY(plot))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(plot.origin.x, y) toPoint:NSMakePoint(NSMaxX(plot), y)];
    }

    if (!side) {
        auto drawPolygon = [&](const std::vector<std::array<float, 2>>& polygon, NSColor* fill, NSColor* stroke) {
            if (polygon.size() < 3u) return;
            NSBezierPath* path = [NSBezierPath bezierPath];
            [path moveToPoint:project(polygon[0][0], polygon[0][1], 0.0f)];
            for (size_t index = 1u; index < polygon.size(); ++index)
                [path lineToPoint:project(polygon[index][0], polygon[index][1], 0.0f)];
            [path closePath];
            [fill setFill];
            [path fill];
            [stroke setStroke];
            [path setLineWidth:1.1];
            [path stroke];
        };
        drawPolygon(snapshot.room.polygon, s3g::clap_gui::color(0x788084, 0.08), s3g::clap_gui::color(0x899194, 0.76));
        for (const auto& branch : snapshot.branches)
            drawPolygon(branch.polygon, s3g::clap_gui::color(0x668b78, 0.08), s3g::clap_gui::color(0x78ad91, 0.62));
    } else {
        NSBezierPath* roomPath = [NSBezierPath bezierPath];
        [roomPath moveToPoint:project(bounds.minimumX, 0.0f, 0.0f)];
        [roomPath lineToPoint:project(bounds.maximumX, 0.0f, 0.0f)];
        if (snapshot.room.ceilingProfile.size() >= 2u) {
            for (auto iterator = snapshot.room.ceilingProfile.rbegin(); iterator != snapshot.room.ceilingProfile.rend(); ++iterator)
                [roomPath lineToPoint:project((*iterator)[0], 0.0f, (*iterator)[1])];
        } else {
            [roomPath lineToPoint:project(bounds.maximumX, 0.0f, snapshot.room.heightMetres)];
            [roomPath lineToPoint:project(bounds.minimumX, 0.0f, snapshot.room.heightMetres)];
        }
        [roomPath closePath];
        [s3g::clap_gui::color(0x788084, 0.08) setFill];
        [roomPath fill];
        [s3g::clap_gui::color(0x899194, 0.76) setStroke];
        [roomPath stroke];
        for (const auto& branch : snapshot.branches) {
            if (branch.polygon.empty()) continue;
            float minimumX = branch.polygon.front()[0];
            float maximumX = minimumX;
            for (const auto& point : branch.polygon) {
                minimumX = std::min(minimumX, point[0]);
                maximumX = std::max(maximumX, point[0]);
            }
            NSRect branchRect = NSMakeRect(project(minimumX, 0.0f, branch.baseHeight + branch.height).x,
                project(0.0f, 0.0f, branch.baseHeight + branch.height).y,
                project(maximumX, 0.0f, 0.0f).x - project(minimumX, 0.0f, 0.0f).x,
                project(0.0f, 0.0f, branch.baseHeight).y - project(0.0f, 0.0f, branch.baseHeight + branch.height).y);
            [s3g::clap_gui::color(0x668b78, 0.07) setFill];
            NSRectFill(branchRect);
            [s3g::clap_gui::color(0x78ad91, 0.54) setStroke];
            NSFrameRect(branchRect);
        }
    }

    const auto& navMin = snapshot.room.navigationMinimumMetres;
    const auto& navMax = snapshot.room.navigationMaximumMetres;
    NSBezierPath* navigation = [NSBezierPath bezierPath];
    if (side) {
        [navigation appendBezierPathWithRect:NSMakeRect(project(navMin.x, 0.0f, navMax.z).x,
            project(0.0f, 0.0f, navMax.z).y,
            project(navMax.x, 0.0f, 0.0f).x - project(navMin.x, 0.0f, 0.0f).x,
            project(0.0f, 0.0f, navMin.z).y - project(0.0f, 0.0f, navMax.z).y)];
    } else {
        [navigation appendBezierPathWithRect:NSMakeRect(project(navMin.x, navMax.y, 0.0f).x,
            project(0.0f, navMax.y, 0.0f).y,
            project(navMax.x, 0.0f, 0.0f).x - project(navMin.x, 0.0f, 0.0f).x,
            project(0.0f, navMin.y, 0.0f).y - project(0.0f, navMax.y, 0.0f).y)];
    }
    const CGFloat dash[] = { 4.0, 3.0 };
    [navigation setLineDash:dash count:2 phase:0.0];
    [s3g::clap_gui::color(0x7199a1, 0.48) setStroke];
    [navigation stroke];

    for (uint32_t index = 0u; index < snapshot.cells.size(); ++index) {
        const auto& cell = snapshot.cells[index];
        const NSPoint point = project(cell.x, cell.y, cell.z);
        [s3g::clap_gui::color(index == snapshot.nearestCell ? 0xa7d3d9 : 0x5f7377,
            index == snapshot.nearestCell ? 0.94 : 0.54) setFill];
        const CGFloat radius = index == snapshot.nearestCell ? 2.6 : 1.6;
        NSRectFill(NSMakeRect(point.x - radius, point.y - radius, radius * 2.0, radius * 2.0));
    }

    const NSPoint listener = project(snapshot.listener.x, snapshot.listener.y, snapshot.listener.z);
    const NSPoint source = project(snapshot.source.x, snapshot.source.y, snapshot.source.z);
    const float worldSpan = side ? std::max(bounds.maximumX - bounds.minimumX, bounds.maximumZ - bounds.minimumZ)
                                 : std::max(bounds.maximumX - bounds.minimumX, bounds.maximumY - bounds.minimumY);
    for (const auto& reflection : snapshot.reflections) {
        const float azimuth = reflection.azimuthDeg * s3g::kPi / 180.0f;
        const float elevation = reflection.elevationDeg * s3g::kPi / 180.0f;
        const float length = worldSpan * (0.14f + 0.32f * s3g::clamp(reflection.delayMs / 220.0f, 0.0f, 1.0f));
        const float dx = std::sin(azimuth) * std::cos(elevation) * length;
        const float dy = std::cos(azimuth) * std::cos(elevation) * length;
        const float dz = std::sin(elevation) * length;
        const NSPoint end = project(snapshot.listener.x + dx, snapshot.listener.y + dy, snapshot.listener.z + dz);
        [s3g::clap_gui::color(0x7fb9c2, s3g::clamp(std::abs(reflection.gain) * 3.2f, 0.12f, 0.54f)) setStroke];
        [NSBezierPath strokeLineFromPoint:listener toPoint:end];
    }
    [s3g::clap_gui::color(0xb8b8b8, 0.62) setStroke];
    [NSBezierPath strokeLineFromPoint:listener toPoint:source];
    [s3g::clap_gui::color(0xb7b7b7) setFill];
    NSRectFill(NSMakeRect(listener.x - 4.0, listener.y - 4.0, 8.0, 8.0));
    [s3g::clap_gui::color(0x90d3dc) setFill];
    NSRectFill(NSMakeRect(source.x - 6.0, source.y - 6.0, 12.0, 12.0));
    [s3g::clap_gui::color(0x101213) setStroke];
    NSFrameRect(NSMakeRect(source.x - 6.0, source.y - 6.0, 12.0, 12.0));
    [@"L" drawAtPoint:NSMakePoint(listener.x + 7.0, listener.y - 8.0) withAttributes:attrs];
    [@"S" drawAtPoint:NSMakePoint(source.x + 9.0, source.y - 8.0) withAttributes:attrs];
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
    const auto params = _plugin->params;
    const float peak = _plugin->outputPeak.exchange(_plugin->outputPeak.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    NSString* peakText = s3g::clap_gui::peakDbText(peak);
    NSString* info = [NSString stringWithFormat:@"MONO > %uOA / 64CH", params.order];
    const CGFloat peakX = kGuiWidth - [peakText sizeWithAttributes:value].width - 18.0;
    const CGFloat infoX = peakX - [info sizeWithAttributes:value].width - 18.0;
    [@"s3g AMBI RAY ENCODER" drawAtPoint:NSMakePoint(18, 13) withAttributes:text];
    [info drawAtPoint:NSMakePoint(infoX, 13) withAttributes:value];
    [peakText drawAtPoint:NSMakePoint(peakX, 13) withAttributes:value];

    const NSRect fieldPanel = NSMakeRect(12, 34, 700, 574);
    const NSRect mapPanel = NSMakeRect(724, 34, 304, 104);
    const NSRect sourcePanel = NSMakeRect(724, 150, 304, 132);
    const NSRect roomPanel = NSMakeRect(724, 294, 304, 214);
    const NSRect outputPanel = NSMakeRect(724, 520, 304, 88);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"RAY FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    [self drawField:snapshot rect:topFieldRect() side:NO attrs:value];
    [self drawField:snapshot rect:sideFieldRect() side:YES attrs:value];
    [@"SRC" drawAtPoint:NSMakePoint(28, 539) withAttributes:text];
    [[NSString stringWithFormat:@"%.2f, %.2f, %.2f m", snapshot.source.x, snapshot.source.y, snapshot.source.z]
        drawAtPoint:NSMakePoint(76, 539) withAttributes:value];
    [@"LIS" drawAtPoint:NSMakePoint(366, 539) withAttributes:text];
    [[NSString stringWithFormat:@"%.2f, %.2f, %.2f m", snapshot.listener.x, snapshot.listener.y, snapshot.listener.z]
        drawAtPoint:NSMakePoint(414, 539) withAttributes:value];
    [@"CELL" drawAtPoint:NSMakePoint(28, 564) withAttributes:text];
    [[NSString stringWithFormat:@"%u / %zu   %.2f m", snapshot.nearestCell + 1u, snapshot.cells.size(), snapshot.nearestDistance]
        drawAtPoint:NSMakePoint(76, 564) withAttributes:value];
    [@"FIELD" drawAtPoint:NSMakePoint(366, 564) withAttributes:text];
    [[NSString stringWithFormat:@"%zu rays / %.1f x %.1f x %.1f m", snapshot.reflections.size(),
        snapshot.room.widthMetres, snapshot.room.depthMetres, snapshot.room.heightMetres]
        drawAtPoint:NSMakePoint(414, 564) withAttributes:value];
    [@"ROOM" drawAtPoint:NSMakePoint(28, 589) withAttributes:text];
    NSString* family = [[NSString stringWithUTF8String:snapshot.family.c_str()] uppercaseString];
    [(family ?: @"ROOM") drawAtPoint:NSMakePoint(76, 589) withAttributes:value];

    s3g::clap_gui::drawPanelFrame(mapPanel.origin.x, mapPanel.origin.y, mapPanel.size.width, mapPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"MAP", true, mapPanel.origin.x, mapPanel.origin.y, mapPanel.size.width, 21, text, style);
    s3g::clap_gui::drawHeaderActionButton(NSMakeRect(951, 36, 64, 17), NSMakeRect(724, 34, 304, 21), @"LOAD", value, style);
    [@"FILE" drawAtPoint:NSMakePoint(742, 68) withAttributes:text];
    [compactFileName(snapshot.name) drawAtPoint:NSMakePoint(790, 68) withAttributes:value];
    [@"STAT" drawAtPoint:NSMakePoint(742, 91) withAttributes:text];
    [[NSString stringWithUTF8String:snapshot.status.c_str()] drawAtPoint:NSMakePoint(790, 91) withAttributes:value];
    [@"CELLS" drawAtPoint:NSMakePoint(742, 114) withAttributes:text];
    [[NSString stringWithFormat:@"%zu", snapshot.cells.size()] drawAtPoint:NSMakePoint(790, 114) withAttributes:value];

    s3g::clap_gui::drawPanelFrame(sourcePanel.origin.x, sourcePanel.origin.y, sourcePanel.size.width, sourcePanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SOURCE", true, sourcePanel.origin.x, sourcePanel.origin.y, sourcePanel.size.width, 21, text, style);
    [self drawSlider:@"X" value:[NSString stringWithFormat:@"%.2f m", snapshot.source.x] norm:params.sourceX y:186 attrs:value style:style];
    [self drawSlider:@"Y" value:[NSString stringWithFormat:@"%.2f m", snapshot.source.y] norm:params.sourceY y:210 attrs:value style:style];
    [self drawSlider:@"Z" value:[NSString stringWithFormat:@"%.2f m", snapshot.source.z] norm:params.sourceZ y:234 attrs:value style:style];
    [self drawSlider:@"MOVE" value:[NSString stringWithFormat:@"%.0f ms", params.movementMs] norm:(params.movementMs - 10.0f) / 490.0f y:258 attrs:value style:style];

    s3g::clap_gui::drawPanelFrame(roomPanel.origin.x, roomPanel.origin.y, roomPanel.size.width, roomPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ROOM FIELD", true, roomPanel.origin.x, roomPanel.origin.y, roomPanel.size.width, 21, text, style);
    [self drawSlider:@"DIR" value:[NSString stringWithFormat:@"%.0f%%", params.direct * 100.0f] norm:params.direct / 1.5f y:332 attrs:value style:style];
    [self drawSlider:@"EAR" value:[NSString stringWithFormat:@"%.0f%%", params.early * 100.0f] norm:params.early / 1.5f y:356 attrs:value style:style];
    [self drawSlider:@"LAT" value:[NSString stringWithFormat:@"%.0f%%", params.late * 100.0f] norm:params.late / 1.5f y:380 attrs:value style:style];
    [self drawSlider:@"SIZE" value:[NSString stringWithFormat:@"%.2f", params.size] norm:(params.size - 0.5f) / 1.5f y:404 attrs:value style:style];
    [self drawSlider:@"SCAT" value:[NSString stringWithFormat:@"%.0f%%", params.scatter * 100.0f] norm:params.scatter y:428 attrs:value style:style];
    [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.2f", params.width] norm:params.width / 1.5f y:452 attrs:value style:style];
    [self drawSlider:@"AIR" value:[NSString stringWithFormat:@"%.0f%%", params.air * 100.0f] norm:params.air y:476 attrs:value style:style];

    s3g::clap_gui::drawPanelFrame(outputPanel.origin.x, outputPanel.origin.y, outputPanel.size.width, outputPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, outputPanel.origin.x, outputPanel.origin.y, outputPanel.size.width, 21, text, style);
    s3g::clap_gui::drawMenu(@"ORD", [NSString stringWithFormat:@"%uOA", params.order], 554, text, value, style, 742, 830, 82);
    s3g::clap_gui::drawToggle(@"BYP", params.bypassRoom, 554, text, value, style, 930, 974, 42);
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", params.outputGainDb] norm:(params.outputGainDb + 60.0f) / 72.0f y:582 attrs:value style:style];

    if (_orderMenuOpen) {
        NSString* items[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
        s3g::clap_gui::drawDropdownMenu(orderMenuRect(), 18, items, 7u,
            static_cast<int>(params.order) - 1, _orderMenuHover, value, style);
    }
}

- (void)resetParam:(clap_id)param
{
    switch (param) {
    case kParamSourceX: [self setParam:param value:0.5]; break;
    case kParamSourceY: [self setParam:param value:0.25]; break;
    case kParamSourceZ: [self setParam:param value:0.5]; break;
    case kParamMovement: [self setParam:param value:60.0]; break;
    case kParamDirect: [self setParam:param value:1.0]; break;
    case kParamEarly: [self setParam:param value:0.72]; break;
    case kParamLate: [self setParam:param value:0.42]; break;
    case kParamSize: [self setParam:param value:1.0]; break;
    case kParamScatter: [self setParam:param value:0.45]; break;
    case kParamWidth: [self setParam:param value:1.0]; break;
    case kParamAir: [self setParam:param value:0.20]; break;
    case kParamOutput: [self setParam:param value:-6.0]; break;
    default: break;
    }
}

- (void)updateSliderAtPoint:(NSPoint)point
{
    const double norm = std::clamp((point.x - 830.0) / 145.0, 0.0, 1.0);
    switch (_dragParam) {
    case kParamSourceX: [self setParam:_dragParam value:norm]; break;
    case kParamSourceY: [self setParam:_dragParam value:norm]; break;
    case kParamSourceZ: [self setParam:_dragParam value:norm]; break;
    case kParamMovement: [self setParam:_dragParam value:10.0 + norm * 490.0]; break;
    case kParamDirect: [self setParam:_dragParam value:norm * 1.5]; break;
    case kParamEarly: [self setParam:_dragParam value:norm * 1.5]; break;
    case kParamLate: [self setParam:_dragParam value:norm * 1.5]; break;
    case kParamSize: [self setParam:_dragParam value:0.5 + norm * 1.5]; break;
    case kParamScatter: [self setParam:_dragParam value:norm]; break;
    case kParamWidth: [self setParam:_dragParam value:norm * 1.5]; break;
    case kParamAir: [self setParam:_dragParam value:norm]; break;
    case kParamOutput: [self setParam:_dragParam value:-60.0 + norm * 72.0]; break;
    default: break;
    }
}

- (void)updateSourceAtPoint:(NSPoint)point view:(NSInteger)view
{
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const GuiWorldBounds bounds = guiWorldBounds(snapshot);
    const NSRect plot = fieldPlotRect(view == 1 ? topFieldRect() : sideFieldRect());
    const float xAmount = s3g::clamp(static_cast<float>((point.x - plot.origin.x) / plot.size.width), 0.0f, 1.0f);
    const float verticalAmount = s3g::clamp(static_cast<float>(1.0 - (point.y - plot.origin.y) / plot.size.height), 0.0f, 1.0f);
    const float worldX = s3g::lerp(bounds.minimumX, bounds.maximumX, xAmount);
    const auto& minimum = snapshot.room.navigationMinimumMetres;
    const auto& maximum = snapshot.room.navigationMaximumMetres;
    [self setParam:kParamSourceX value:(worldX - minimum.x) / std::max(0.0001f, maximum.x - minimum.x)];
    if (view == 1) {
        const float worldY = s3g::lerp(bounds.minimumY, bounds.maximumY, verticalAmount);
        [self setParam:kParamSourceY value:(worldY - minimum.y) / std::max(0.0001f, maximum.y - minimum.y)];
    } else {
        const float worldZ = s3g::lerp(bounds.minimumZ, bounds.maximumZ, verticalAmount);
        [self setParam:kParamSourceZ value:(worldZ - minimum.z) / std::max(0.0001f, maximum.z - minimum.z)];
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_orderMenuOpen) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, orderMenuRect(), 18, 7u);
        _orderMenuOpen = false;
        _orderMenuHover = -1;
        if (hit >= 0) [self setParam:kParamOrder value:hit + 1];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(951, 36, 64, 17))) {
        [self loadRayField];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(830, 552, 82, 18))) {
        _orderMenuOpen = true;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(974, 552, 42, 18))) {
        [self setParam:kParamBypass value:_plugin->params.bypassRoom ? 0.0 : 1.0];
        return;
    }
    if (NSPointInRect(point, fieldPlotRect(topFieldRect()))) {
        _sourceDragView = 1;
        [self updateSourceAtPoint:point view:_sourceDragView];
        return;
    }
    if (NSPointInRect(point, fieldPlotRect(sideFieldRect()))) {
        _sourceDragView = 2;
        [self updateSourceAtPoint:point view:_sourceDragView];
        return;
    }
    struct Row { clap_id param; CGFloat y; };
    const Row rows[] = {
        { kParamSourceX, 186 }, { kParamSourceY, 210 }, { kParamSourceZ, 234 }, { kParamMovement, 258 },
        { kParamDirect, 332 }, { kParamEarly, 356 }, { kParamLate, 380 }, { kParamSize, 404 },
        { kParamScatter, 428 }, { kParamWidth, 452 }, { kParamAir, 476 }, { kParamOutput, 582 }
    };
    for (const auto& row : rows) {
        if (!NSPointInRect(point, NSMakeRect(734, row.y - 8, 286, 24))) continue;
        if ([event clickCount] >= 2) {
            [self resetParam:row.param];
            return;
        }
        _dragParam = row.param;
        [self updateSliderAtPoint:point];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_sourceDragView != 0) {
        [self updateSourceAtPoint:point view:_sourceDragView];
        return;
    }
    if (_dragParam != CLAP_INVALID_ID) [self updateSliderAtPoint:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _sourceDragView = 0;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_orderMenuOpen) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _orderMenuHover = s3g::clap_gui::dropdownHitIndex(point, orderMenuRect(), 18, 7u);
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_orderMenuHover == -1) return;
    _orderMenuHover = -1;
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
    instance->guiView = [[S3GAmbiRayEncoderView alloc] initWithPlugin:instance];
    return instance->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return;
    instance->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GAmbiRayEncoderView*>(instance->guiView);
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
bool guiShow(const clap_plugin_t* plugin) { auto* instance = self(plugin); if (!instance->guiView) return false; instance->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(instance->guiView) setHidden:NO]; [static_cast<S3GAmbiRayEncoderView*>(instance->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* instance = self(plugin); if (!instance->guiView) return false; instance->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbiRayEncoderView*>(instance->guiView) stopRefreshTimer]; [static_cast<NSView*>(instance->guiView) setHidden:YES]; return true; }

const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    CLAP_PLUGIN_FEATURE_REVERB,
    nullptr
};

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
    instance->hostTail = host && host->get_extension
        ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL))
        : nullptr;
    const s3g::Vec3 normalized = s3g::AmbiRayEncoder::normalizedSourcePosition(
        instance->descriptor, instance->descriptor.defaultSourcePositionMetres);
    instance->params.sourceX = normalized.x;
    instance->params.sourceY = normalized.y;
    instance->params.sourceZ = normalized.z;
    instance->params = s3g::sanitizeAmbiRayEncoderParams(instance->params);
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
