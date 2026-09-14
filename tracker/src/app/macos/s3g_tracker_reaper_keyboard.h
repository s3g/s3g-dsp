#pragma once
#import <Cocoa/Cocoa.h>
#include <memory>

namespace s3g::tracker::editor {
// First field of SWELL MSG; the rest is deliberately never inspected.
struct ReaperKeyboardMessage { void* window = nullptr; };
// REAPER SDK accelerator_register_t ABI, with the unused MSG pointer opaque.
// https://github.com/justinfrankel/reaper-sdk/blob/main/sdk/reaper_plugin.h
struct ReaperKeyboardAccelerator {
    int (*translate)(void*, ReaperKeyboardAccelerator*) = nullptr;
    bool isLocal = true;
    void* user = nullptr;
};
// UI-thread-only registration. Host hooks precede NSView key equivalents.
// The retained page list also covers pages reparented into detached windows.
class MacReaperTextInput {
public:
    using Register = int (*)(const char*, void*);
    MacReaperTextInput(Register, NSArray<NSView*>* pages);
    ~MacReaperTextInput();
    MacReaperTextInput(const MacReaperTextInput&) = delete;
    MacReaperTextInput& operator=(const MacReaperTextInput&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
