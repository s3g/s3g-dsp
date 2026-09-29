// Isolated, bounded NEON mode-feedback diagnostic. No plugin installation,
// device-property writes, SysEx, firmware, pad notes, or host-routing changes.
// Default is inventory/dry-run; output requires --apply AND an exact endpoint
// UID. Stop playing and turn Sample Neon's NEON OWNER off before --apply.
// Build: clang++ -std=c++17 -Wall -Wextra -Wpedantic -framework Foundation \
//   -framework CoreMIDI scripts/neon-mode-feedback-check.mm -o /tmp/neon-mode-feedback-check
// Example: /tmp/neon-mode-feedback-check --uid 341027543 --mode stack
// Add --apply to send four mode commands 20 ms apart, then listen for 180 s.
#import <Foundation/Foundation.h>
#import <CoreMIDI/CoreMIDI.h>
#include "../dsp/s3g_reloop_neon.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

namespace {
namespace neon = s3g::controller::reloop_neon;
std::string name(MIDIObjectRef object) {
    CFStringRef text = nullptr;
    if (MIDIObjectGetStringProperty(object,kMIDIPropertyDisplayName,&text)!=noErr || !text)
        MIDIObjectGetStringProperty(object,kMIDIPropertyName,&text);
    if (!text) return {};
    char buffer[512] {};
    CFStringGetCString(text,buffer,sizeof(buffer),kCFStringEncodingUTF8);
    CFRelease(text);return buffer;
}
SInt32 property(MIDIObjectRef object,CFStringRef key) {
    SInt32 result=0;MIDIObjectGetIntegerProperty(object,key,&result);return result;
}
bool isNeon(MIDIEndpointRef endpoint) {
    auto label=name(endpoint);
    std::transform(label.begin(),label.end(),label.begin(),[](unsigned char c){return std::toupper(c);});
    return label.find("NEON")!=std::string::npos && !property(endpoint,kMIDIPropertyOffline);
}
struct Capture {
    std::mutex mutex;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    unsigned packets=0;
};
void receive(const MIDIPacketList* list,void* context,void*) {
    auto& capture=*static_cast<Capture*>(context);
    std::lock_guard<std::mutex> lock(capture.mutex);
    const auto* packet=&list->packet[0];
    for(UInt32 i=0;i<list->numPackets;++i,packet=MIDIPacketNext(packet)) {
        if(capture.packets++>=4096) continue;
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-capture.start).count();
        std::printf("%8.3f IN :",seconds);
        for(unsigned n=0;n<std::min<unsigned>(packet->length,256);++n) std::printf(" %02X",packet->data[n]);
        std::printf("\n");
    }
    std::fflush(stdout);
}
bool send(MIDIPortRef port,MIDIEndpointRef endpoint,neon::MidiMessage message) {
    MIDIPacketList list {};
    auto* packet=MIDIPacketListInit(&list);
    const Byte bytes[] {message.status,message.data1,message.data2};
    if(!MIDIPacketListAdd(&list,sizeof(list),packet,0,sizeof(bytes),bytes)) return false;
    return MIDISend(port,endpoint,&list)==noErr;
}
}
int main(int argc,char** argv) {
    @autoreleasepool {
        bool apply=false,haveUid=false,haveMode=false;
        SInt32 uid=0;
        neon::Mode mode=neon::Mode::HotCue;
        for(int i=1;i<argc;++i) {
            if(std::strcmp(argv[i],"--apply")==0) apply=true;
            else if(std::strcmp(argv[i],"--uid")==0 && i+1<argc) {
                char* end=nullptr;errno=0;const auto value=std::strtoll(argv[++i],&end,10);
                if(errno || !end || *end || !value || value<std::numeric_limits<SInt32>::min()
                    || value>std::numeric_limits<SInt32>::max()) return 2;
                uid=static_cast<SInt32>(value);haveUid=true;
            } else if(std::strcmp(argv[i],"--mode")==0 && i+1<argc) {
                const std::string selected=argv[++i];haveMode=true;
                if(selected=="play") mode=neon::Mode::Sampler;
                else if(selected=="chop") mode=neon::Mode::Slicer;
                else if(selected=="stack") mode=neon::Mode::HotCue;
                else if(selected=="resample") mode=neon::Mode::HotLoop;
                else return 2;
            } else {
                std::fprintf(stderr,"Usage: %s [--uid DESTINATION_UID --mode play|chop|stack|resample [--apply]]\n",argv[0]);
                return 2;
            }
        }
        if(apply && (!haveUid || !haveMode)) {
            std::fprintf(stderr,"Output requires an exact --uid and explicit --mode.\n");return 2;
        }
        MIDIClientRef client=0;
        if(MIDIClientCreate(CFSTR("s3g isolated NEON mode check"),nullptr,nullptr,&client)!=noErr) return 1;
        MIDIEndpointRef destination=0,source=0;
        MIDIEntityRef selectedEntity=0;
        unsigned matches=0,sourceMatches=0;
        for(ItemCount n=0;n<MIDIGetNumberOfDestinations();++n) {
            const auto endpoint=MIDIGetDestination(n);
            if(!isNeon(endpoint)) continue;
            const auto endpointUid=property(endpoint,kMIDIPropertyUniqueID);
            std::printf("DESTINATION %d [%s]\n",endpointUid,name(endpoint).c_str());
            if(haveUid && endpointUid==uid) {
                destination=endpoint;++matches;MIDIEndpointGetEntity(endpoint,&selectedEntity);
            }
        }
        if(matches==1 && selectedEntity) for(ItemCount n=0;n<MIDIGetNumberOfSources();++n) {
            const auto endpoint=MIDIGetSource(n);MIDIEntityRef entity=0;MIDIEndpointGetEntity(endpoint,&entity);
            if(entity==selectedEntity && isNeon(endpoint)) {source=endpoint;++sourceMatches;}
        }
        if(haveUid && (matches!=1 || sourceMatches!=1)) {
            std::fprintf(stderr,"Refusing ambiguous, absent or unpaired destination %d.\n",uid);
            MIDIClientDispose(client);return 1;
        }
        if(haveUid) std::printf("PAIRED SOURCE %d [%s]\n",property(source,kMIDIPropertyUniqueID),name(source).c_str());
        if(haveMode) for(uint8_t bank=0;bank<4;++bank) {
            const auto m=neon::modeLedMessage(bank,mode,neon::Layer::First);
            std::printf("PLANNED %02X %02X %02X / bank %c / 20 ms spacing\n",m.status,m.data1,m.data2,'A'+bank);
        }
        std::fflush(stdout);
        if(!apply) {std::puts("DRY RUN: nothing sent.");MIDIClientDispose(client);return 0;}
        Capture capture;MIDIPortRef input=0,output=0;
        int result=0;
        if(MIDIInputPortCreate(client,CFSTR("mode check input"),receive,&capture,&input)!=noErr
            || MIDIPortConnectSource(input,source,nullptr)!=noErr
            || MIDIOutputPortCreate(client,CFSTR("mode check output"),&output)!=noErr) result=1;
        if(!result) {
            for(uint8_t bank=0;bank<4;++bank) {
                const auto m=neon::modeLedMessage(bank,mode,neon::Layer::First);
                if(!send(output,destination,m)) {result=1;break;}
                std::printf("SENT %02X %02X %02X\n",m.status,m.data1,m.data2);std::fflush(stdout);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if(!result) {
                std::puts("Listening 180 seconds on this unit only. Change banks and tap pad 1; no further output will be sent.");
                std::fflush(stdout);
                const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(180);
                while(std::chrono::steady_clock::now()<until) CFRunLoopRunInMode(kCFRunLoopDefaultMode,.1,false);
            }
        }
        if(input) {MIDIPortDisconnectSource(input,source);MIDIPortDispose(input);}
        if(output) MIDIPortDispose(output);
        MIDIClientDispose(client);
        std::printf("END / %u input packets / status %d. No automatic restore commands sent.\n",capture.packets,result);
        return result;
    }
}
