#include "s3g_reloop_neon.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<uint32_t> messageCount { 0u };

std::string stringProperty(MIDIObjectRef object, CFStringRef property)
{
    CFStringRef value = nullptr;
    if (MIDIObjectGetStringProperty(object, property, &value) != noErr
        || !value) return {};
    std::array<char, 512u> buffer {};
    const bool converted = CFStringGetCString(value, buffer.data(),
        static_cast<CFIndex>(buffer.size()), kCFStringEncodingUTF8);
    CFRelease(value);
    return converted ? std::string(buffer.data()) : std::string();
}

std::string endpointName(MIDIEndpointRef endpoint)
{
    std::string name = stringProperty(endpoint, kMIDIPropertyDisplayName);
    if (name.empty()) name = stringProperty(endpoint, kMIDIPropertyName);
    return name;
}

bool containsNeon(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value.find("NEON") != std::string::npos;
}

const char* actionName(s3g::controller::reloop_neon::ActionType type)
{
    using Type = s3g::controller::reloop_neon::ActionType;
    switch (type) {
    case Type::Pad: return "pad";
    case Type::PadPressure: return "pressure";
    case Type::PadVelocity: return "velocity";
    case Type::SelectBank: return "bank";
    case Type::SelectMode: return "mode";
    case Type::EncoderTurn: return "encoder-turn";
    case Type::EncoderPush: return "encoder-push";
    case Type::Utility: return "utility";
    case Type::None: break;
    }
    return "unmapped";
}

void printMessage(uint8_t status, uint8_t data1, uint8_t data2)
{
    const auto action = s3g::controller::reloop_neon::decode({
        status, data1, data2,
    });
    std::printf("%02X %02X %02X  %-12s bank=%d pad=%d value=%u%s%s\n",
        status, data1, data2, actionName(action.type),
        action.bank == 0xffu ? -1 : static_cast<int>(action.bank),
        action.pad == 0xffu ? -1 : static_cast<int>(action.pad),
        static_cast<unsigned>(action.value),
        action.pressed ? " pressed" : "",
        action.shifted ? " shifted" : "");
    std::fflush(stdout);
    messageCount.fetch_add(1u, std::memory_order_relaxed);
}

void midiRead(const MIDIPacketList* packets, void*, void*)
{
    if (!packets) return;
    const MIDIPacket* packet = &packets->packet[0u];
    for (UInt32 packetIndex = 0u; packetIndex < packets->numPackets;
         ++packetIndex) {
        std::size_t index = 0u;
        while (index < packet->length) {
            const uint8_t status = packet->data[index];
            if (status == 0xf0u) {
                std::printf("SYSEX");
                while (index < packet->length) {
                    const uint8_t byte = packet->data[index++];
                    std::printf(" %02X", byte);
                    if (byte == 0xf7u) break;
                }
                std::printf("\n");
                messageCount.fetch_add(1u, std::memory_order_relaxed);
                continue;
            }
            const uint8_t kind = static_cast<uint8_t>(status & 0xf0u);
            const std::size_t length = kind == 0xc0u || kind == 0xd0u
                ? 2u : 3u;
            if (index + length > packet->length) break;
            printMessage(status, packet->data[index + 1u],
                length == 3u ? packet->data[index + 2u] : 0u);
            index += length;
        }
        packet = MIDIPacketNext(packet);
    }
}

bool sendBytes(MIDIPortRef port, MIDIEndpointRef destination,
    const uint8_t* bytes, std::size_t size)
{
    if (!port || !destination || !bytes || size == 0u || size > 256u)
        return false;
    std::array<uint8_t, 512u> storage {};
    auto* list = reinterpret_cast<MIDIPacketList*>(storage.data());
    MIDIPacket* packet = MIDIPacketListInit(list);
    packet = MIDIPacketListAdd(list, storage.size(), packet, 0u,
        static_cast<UInt16>(size), bytes);
    return packet && MIDISend(port, destination, list) == noErr;
}

bool sendMessages(MIDIPortRef port, MIDIEndpointRef destination,
    const std::vector<s3g::controller::reloop_neon::MidiMessage>& messages)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(messages.size() * 3u);
    for (const auto& message : messages) {
        bytes.push_back(message.status);
        bytes.push_back(message.data1);
        bytes.push_back(message.data2);
    }
    return sendBytes(port, destination, bytes.data(), bytes.size());
}

std::vector<s3g::controller::reloop_neon::MidiMessage> ledFrame(
    uint8_t value, int activePad = -1)
{
    using namespace s3g::controller::reloop_neon;
    std::vector<MidiMessage> messages;
    messages.reserve(kMaximumLedMessages);
    messages.push_back(bankLedMessage(0u, Mode::Sampler));
    messages.push_back(modeLedMessage(0u, Mode::Sampler, Layer::First));
    for (uint8_t pad = 0u; pad < kPadsPerBank; ++pad) {
        const uint8_t padValue = activePad < 0
            || activePad == static_cast<int>(pad) ? value : 0u;
        messages.push_back(padSurfaceMessage(
            0u, Mode::Sampler, Layer::First, pad, padValue));
        for (uint8_t segment = 0u; segment < kLedSegmentsPerPad; ++segment)
            messages.push_back(padLedMessage(pad, segment, padValue));
    }
    return messages;
}

void usage()
{
    std::fprintf(stderr,
        "usage: s3g_reloop_neon_probe [--monitor SECONDS] [--led-test] "
        "[--clear]\n"
        "With no options, lists CoreMIDI sources and destinations.\n");
}

} // namespace

int main(int argc, char** argv)
{
    double monitorSeconds = 0.0;
    bool testLeds = false;
    bool clearLeds = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--monitor") == 0
            && index + 1 < argc) {
            char* end = nullptr;
            monitorSeconds = std::strtod(argv[++index], &end);
            if (!end || end == argv[index] || monitorSeconds < 0.0) {
                usage();
                return 2;
            }
        } else if (std::strcmp(argv[index], "--led-test") == 0) {
            testLeds = true;
        } else if (std::strcmp(argv[index], "--clear") == 0) {
            clearLeds = true;
        } else {
            usage();
            return 2;
        }
    }

    MIDIClientRef client = 0u;
    MIDIPortRef inputPort = 0u;
    MIDIPortRef outputPort = 0u;
    if (MIDIClientCreate(CFSTR("s3g Reloop Neon Probe"), nullptr, nullptr,
            &client) != noErr
        || MIDIInputPortCreate(client, CFSTR("Neon Monitor"), midiRead,
            nullptr, &inputPort) != noErr
        || MIDIOutputPortCreate(client, CFSTR("Neon Test"),
            &outputPort) != noErr) {
        std::fprintf(stderr, "Could not create CoreMIDI client/ports\n");
        if (client) MIDIClientDispose(client);
        return 1;
    }

    MIDIEndpointRef neonSource = 0u;
    MIDIEndpointRef neonDestination = 0u;
    std::printf("CoreMIDI sources:\n");
    for (ItemCount index = 0u; index < MIDIGetNumberOfSources(); ++index) {
        const MIDIEndpointRef endpoint = MIDIGetSource(index);
        const std::string name = endpointName(endpoint);
        std::printf("  [%lu] %s%s\n", static_cast<unsigned long>(index),
            name.c_str(), containsNeon(name) ? "  <NEON>" : "");
        if (!neonSource && containsNeon(name)) neonSource = endpoint;
    }
    std::printf("CoreMIDI destinations:\n");
    for (ItemCount index = 0u; index < MIDIGetNumberOfDestinations();
         ++index) {
        const MIDIEndpointRef endpoint = MIDIGetDestination(index);
        const std::string name = endpointName(endpoint);
        std::printf("  [%lu] %s%s\n", static_cast<unsigned long>(index),
            name.c_str(), containsNeon(name) ? "  <NEON>" : "");
        if (!neonDestination && containsNeon(name))
            neonDestination = endpoint;
    }

    bool ok = true;
    if (monitorSeconds > 0.0) {
        if (!neonSource || MIDIPortConnectSource(inputPort, neonSource,
                nullptr) != noErr) {
            std::fprintf(stderr, "No usable Neon MIDI source found\n");
            ok = false;
        } else {
            std::printf("Monitoring NEON for %.1f seconds...\n",
                monitorSeconds);
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, monitorSeconds, false);
            MIDIPortDisconnectSource(inputPort, neonSource);
            std::printf("Captured %u MIDI messages\n",
                messageCount.load(std::memory_order_relaxed));
        }
    }

    if (testLeds || clearLeds) {
        if (!neonDestination) {
            std::fprintf(stderr, "No usable Neon MIDI destination found\n");
            ok = false;
        } else {
            const auto sysex =
                s3g::controller::reloop_neon::enableFourDecksSysEx();
            ok = sendBytes(outputPort, neonDestination,
                sysex.data(), sysex.size()) && ok;
            if (testLeds) {
                for (int pad = 0; pad < 8; ++pad) {
                    ok = sendMessages(outputPort, neonDestination,
                        ledFrame(127u, pad)) && ok;
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(120));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            ok = sendMessages(outputPort, neonDestination,
                ledFrame(0u)) && ok;
            std::printf(testLeds ? "LED chase sent and cleared\n"
                                 : "LED clear sent\n");
        }
    }

    MIDIPortDispose(inputPort);
    MIDIPortDispose(outputPort);
    MIDIClientDispose(client);
    return ok ? 0 : 1;
}
