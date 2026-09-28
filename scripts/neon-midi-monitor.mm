// Read-only macOS NEON endpoint inventory / raw input capture. Never opens an
// output port, sends MIDI, changes device properties or edits host routing.
// Build: clang++ -std=c++17 -framework Foundation -framework CoreMIDI \
//   scripts/neon-midi-monitor.mm -o /tmp/s3g-neon-midi-monitor
// Run: /tmp/s3g-neon-midi-monitor [--capture SECONDS]
#import <Foundation/Foundation.h>
#import <CoreMIDI/CoreMIDI.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::string name(MIDIObjectRef object) {
    CFStringRef value = nullptr;
    if (MIDIObjectGetStringProperty(object, kMIDIPropertyDisplayName, &value) != noErr || !value)
        MIDIObjectGetStringProperty(object, kMIDIPropertyName, &value);
    if (!value) return {};
    char text[512] {};
    CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8);
    CFRelease(value); return text;
}
bool isNeon(const std::string& text) {
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
    return upper.find("NEON") != std::string::npos;
}
SInt32 property(MIDIObjectRef object, CFStringRef key) {
    SInt32 result = 0;
    MIDIObjectGetIntegerProperty(object, key, &result); return result;
}
struct Source { MIDIEndpointRef endpoint; SInt32 uid; std::string label; };
struct Capture {
    std::mutex mutex;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    size_t packets = 0;
    static constexpr size_t limit = 4096;
};
void receive(const MIDIPacketList* list, void* context, void* connection) {
    auto& capture = *static_cast<Capture*>(context);
    const auto& source = *static_cast<Source*>(connection);
    std::lock_guard<std::mutex> guard(capture.mutex);
    auto* packet = &list->packet[0];
    for (UInt32 i = 0; i < list->numPackets; ++i, packet = MIDIPacketNext(packet)) {
        if (capture.packets++ >= Capture::limit) continue;
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - capture.start).count();
        std::printf("%8.3f SOURCE %d [%s] timestamp=%llu bytes=%u :", seconds, source.uid,
            source.label.c_str(), static_cast<unsigned long long>(packet->timeStamp), packet->length);
        for (unsigned n = 0; n < std::min<unsigned>(packet->length, 256); ++n)
            std::printf(" %02X", packet->data[n]);
        if (packet->length > 256) std::printf(" ... [packet truncated]");
        std::printf("\n");
    }
    std::fflush(stdout);
}
}
int main(int argc, char** argv) {
    @autoreleasepool {
        double duration = 0;
        if (argc != 1) {
            char* end = nullptr;
            if (argc == 3 && std::strcmp(argv[1], "--capture") == 0) duration = std::strtod(argv[2], &end);
            if (!end || *end || !(duration > 0 && duration <= 300)) {
                std::fprintf(stderr, "Usage: %s [--capture SECONDS (1-300)]\n", argv[0]); return 2;
            }
        }
        MIDIClientRef client = 0;
        const OSStatus created = MIDIClientCreate(CFSTR("s3g NEON read-only diagnostic"), nullptr, nullptr, &client);
        if (created != noErr) { std::fprintf(stderr, "CoreMIDI client error: %d\n", created); return 1; }
        std::vector<Source> sources;
        for (bool input : {true, false}) {
            const ItemCount count = input ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
            for (ItemCount i = 0; i < count; ++i) {
                const auto endpoint = input ? MIDIGetSource(i) : MIDIGetDestination(i);
                const auto label = name(endpoint);
                if (!isNeon(label)) continue;
                const auto uid = property(endpoint, kMIDIPropertyUniqueID);
                MIDIEntityRef entity = 0; MIDIEndpointGetEntity(endpoint, &entity);
                MIDIDeviceRef device = 0; if (entity) MIDIEntityGetDevice(entity, &device);
                std::printf("%s uid=%d name=[%s] entity=[%s] entity_uid=%d device=[%s] device_uid=%d offline=%d\n",
                    input ? "SOURCE" : "DESTINATION", uid, label.c_str(), name(entity).c_str(),
                    entity ? property(entity, kMIDIPropertyUniqueID) : 0,
                    name(device).c_str(), device ? property(device, kMIDIPropertyUniqueID) : 0,
                    property(endpoint, kMIDIPropertyOffline));
                if (input) sources.push_back({endpoint,uid,label});
            }
        }
        std::printf("NEON input endpoints: %zu. Read-only; no MIDI output.\n", sources.size()); std::fflush(stdout);
        int result = 0;
        if (duration > 0 && !sources.empty()) {
            Capture capture;
            MIDIPortRef port = 0;
            const auto status = MIDIInputPortCreate(client, CFSTR("NEON diagnostic input"), receive, &capture, &port);
            if (status != noErr) { std::fprintf(stderr, "Input port error: %d\n", status); result = 1; }
            else {
                for (auto& source : sources) {
                    const auto connected = MIDIPortConnectSource(port, source.endpoint, &source);
                    if (connected != noErr) { std::fprintf(stderr, "Connect %d error: %d\n", source.uid, connected); result = 1; }
                }
                std::printf("CAPTURE START / %.0f seconds / at most %zu packets printed\n", duration, Capture::limit);
                std::fflush(stdout);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(duration);
                while (std::chrono::steady_clock::now() < deadline)
                    CFRunLoopRunInMode(kCFRunLoopDefaultMode, .1, false);
                for (auto& source : sources) MIDIPortDisconnectSource(port, source.endpoint);
                MIDIPortDispose(port);
                std::lock_guard<std::mutex> guard(capture.mutex);
                std::printf("CAPTURE END / %zu packets observed%s\n", capture.packets,
                    capture.packets > Capture::limit ? " (print limit exceeded)" : "");
            }
        } else if (duration > 0) result = 1;
        MIDIClientDispose(client); return result;
    }
}
