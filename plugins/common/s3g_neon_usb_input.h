#pragma once

#include "s3g_reloop_neon.h"
#include "s3g_neon_midi_stream.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#endif

namespace s3g::controller::neon_midi {

// MIDI I/O is never opened on the audio thread. CoreMIDI producers serialize
// off-thread; the audio consumer uses a bounded, allocation-free SPSC queue.
// Every packet retains its unit and connection generation. A disconnect or
// overflow invalidates stale packets and asks the consumer to release that unit.
class UsbInput {
public:
    struct Event {
        reloop_neon::MidiMessage midi {};
        uint8_t unit = 0;
        uint32_t generation = 0;
        uint64_t timestamp = 0;
        bool bankPageRestore = false;
    };
    std::array<std::atomic<int32_t>, 2> source {}, destination {};
    std::array<std::atomic<uint32_t>, 2> generation {};
    std::array<std::atomic<bool>, 2> connected {};
    std::atomic<unsigned> wanted {0}, errors {0};
    std::atomic<unsigned> assignmentRequest {0}; // 1 swap, 2 forget/relearn.
    UsbInput() = default;
    UsbInput(const UsbInput&) = delete;
    UsbInput& operator=(const UsbInput&) = delete;
    ~UsbInput() { stop(); }
    bool pop(Event& event) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        event = queue_[r % queue_.size()];
        read_.store(r+1, std::memory_order_release); return true;
    }
    void start() {
#if defined(__APPLE__)
        const auto* disabled = std::getenv("S3G_SAMPLE_NEON_DISABLE_DIRECT_MIDI");
        if (worker_.joinable() || (disabled && std::strcmp(disabled, "0"))) return;
        stopping_.store(false); worker_ = std::thread([this] { run(); });
#endif
    }
    void stop() {
#if defined(__APPLE__)
        stopping_.store(true); if (worker_.joinable()) worker_.join();
#endif
    }
    static uint64_t clock() noexcept {
#if defined(__APPLE__)
        return mach_absolute_time();
#else
        return 0;
#endif
    }
    double ticksPerSecond() const noexcept { return ticksPerSecond_.load(); }
private:
    std::array<Event, 4096> queue_ {};
    std::atomic<uint64_t> read_ {0}, write_ {0};
    std::atomic<double> ticksPerSecond_ {1.0};
#if defined(__APPLE__)
    struct Connection {
        UsbInput* owner = nullptr;
        unsigned unit = 0;
        MIDIEndpointRef endpoint = 0;
        int32_t uid = 0;
        InputPacketDecoder decoder;
    };
    std::array<Connection, 2> connections_ {};
    std::mutex producer_;
    std::atomic<bool> stopping_ {false};
    std::thread worker_;
    struct Claim { int32_t source; UsbInput* owner; };
    inline static std::mutex claimsMutex_;
    inline static std::vector<Claim> claims_;
    static int32_t property(MIDIObjectRef object, CFStringRef key) {
        SInt32 value = 0; if (object) MIDIObjectGetIntegerProperty(object, key, &value); return value;
    }
    static bool isNeon(MIDIEndpointRef endpoint) {
        CFStringRef name = nullptr;
        MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name);
        if (!name) MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name);
        if (!name) return false;
        const bool match = CFStringFind(name, CFSTR("NEON"), kCFCompareCaseInsensitive).location != kCFNotFound;
        CFRelease(name); return match;
    }
    static int32_t pairedOutput(MIDIEndpointRef input) {
        MIDIEntityRef entity = 0; MIDIEndpointGetEntity(input, &entity);
        if (!entity) return 0;
        // Match the entity, never the (identical) display name or list index.
        for (ItemCount n = 0; n < MIDIEntityGetNumberOfDestinations(entity); ++n) {
            const auto output = MIDIEntityGetDestination(entity, n);
            if (!property(output, kMIDIPropertyOffline)) return property(output, kMIDIPropertyUniqueID);
        }
        return 0;
    }
    bool claim(int32_t uid) {
        std::lock_guard<std::mutex> guard(claimsMutex_);
        for (const auto& entry : claims_) if (entry.source == uid) return entry.owner == this;
        claims_.push_back({uid, this}); return true;
    }
    void unclaim(int32_t uid) {
        std::lock_guard<std::mutex> guard(claimsMutex_);
        claims_.erase(std::remove_if(claims_.begin(), claims_.end(),
            [&](const Claim& c) { return c.owner == this && c.source == uid; }), claims_.end());
    }
    void enqueue(Connection& c, reloop_neon::MidiMessage midi, uint64_t timestamp, bool bankPageRestore) {
        const auto w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= queue_.size()) {
            generation[c.unit].fetch_add(1); errors.fetch_add(1); return;
        }
        queue_[w % queue_.size()] = {midi, static_cast<uint8_t>(c.unit), generation[c.unit].load(), timestamp, bankPageRestore};
        write_.store(w+1, std::memory_order_release);
    }
    static void receive(const MIDIPacketList* list, void*, void* context) {
        auto& c = *static_cast<Connection*>(context);
        auto& p = *c.owner;
        std::lock_guard<std::mutex> guard(p.producer_);
        if (!p.connected[c.unit].load() || c.unit >= p.wanted.load() || p.source[c.unit].load() != c.uid) return;
        const auto* packet = &list->packet[0];
        for (UInt32 n = 0; n < list->numPackets; ++n, packet = MIDIPacketNext(packet)) {
            c.decoder.packet(packet->data, packet->length, [&](reloop_neon::MidiMessage message, bool restored) {
                p.enqueue(c, message, packet->timeStamp ? packet->timeStamp : clock(), restored);
            });
        }
    }
    void disconnect(MIDIPortRef port, unsigned unit) {
        auto& c = connections_[unit];
        connected[unit].store(false);
        if (c.endpoint) MIDIPortDisconnectSource(port, c.endpoint);
        std::lock_guard<std::mutex> guard(producer_);
        if (c.uid) unclaim(c.uid);
        c.endpoint = 0; c.uid = 0;
        c.decoder.reset();
        destination[unit].store(0); generation[unit].fetch_add(1);
    }
    void run() {
        mach_timebase_info_data_t timebase {}; mach_timebase_info(&timebase);
        ticksPerSecond_.store(1.e9 * timebase.denom / timebase.numer);
        MIDIClientRef client = 0; MIDIPortRef port = 0;
        if (MIDIClientCreate(CFSTR("s3g NEON USB input"), nullptr, nullptr, &client) != noErr
            || MIDIInputPortCreate(client, CFSTR("NEON units"), receive, this, &port) != noErr) {
            errors.fetch_add(1); if (client) MIDIClientDispose(client); return;
        }
        for (unsigned u = 0; u < 2; ++u) { connections_[u].owner = this; connections_[u].unit = u; }
        while (!stopping_.load()) {
            if (const auto request = assignmentRequest.exchange(0)) {
                for (unsigned u = 0; u < 2; ++u) disconnect(port, u);
                const auto first = source[0].load();
                source[0].store(request == 1 ? source[1].load() : 0);
                source[1].store(request == 1 ? first : 0);
            }
            const unsigned count = std::min(wanted.load(), 2u);
            for (unsigned u = 0; u < 2; ++u) {
                auto& c = connections_[u];
                MIDIEndpointRef found = 0;
                if (u < count) for (ItemCount n = 0; n < MIDIGetNumberOfSources(); ++n) {
                    const auto endpoint = MIDIGetSource(n);
                    if (!isNeon(endpoint) || property(endpoint, kMIDIPropertyOffline)) continue;
                    const auto uid = property(endpoint, kMIDIPropertyUniqueID);
                    if (!uid || uid == source[1-u].load()) continue;
                    if (source[u].load() && source[u].load() != uid) continue;
                    if (!claim(uid)) continue;
                    source[u].store(uid); found = endpoint; break;
                }
                if (c.endpoint != found) {
                    disconnect(port, u);
                    if (found) {
                        if (!claim(property(found, kMIDIPropertyUniqueID))) continue;
                        {
                            std::lock_guard<std::mutex> guard(producer_);
                            c.endpoint = found;
                            c.uid = property(found, kMIDIPropertyUniqueID);
                            destination[u].store(pairedOutput(found)); generation[u].fetch_add(1);
                            connected[u].store(true);
                        }
                        if (MIDIPortConnectSource(port, found, &c) != noErr) {
                            disconnect(port, u); errors.fetch_add(1);
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        for (unsigned u = 0; u < 2; ++u) disconnect(port, u);
        MIDIPortDispose(port); MIDIClientDispose(client);
    }
#endif
};
} // namespace s3g::controller::neon_midi
