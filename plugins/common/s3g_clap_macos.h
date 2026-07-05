#pragma once

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>

namespace s3g::clap_support {

inline void beginRealtimeActivity(void*& activityToken)
{
    if (activityToken) {
        return;
    }
    id activity = [[NSProcessInfo processInfo] beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                                                                 reason:@"s3g-dsp realtime audio processing"];
    activityToken = [activity retain];
}

inline void endRealtimeActivity(void*& activityToken)
{
    if (!activityToken) {
        return;
    }
    id activity = static_cast<id>(activityToken);
    [[NSProcessInfo processInfo] endActivity:activity];
    [activity release];
    activityToken = nullptr;
}

inline bool hostAppIsActive()
{
    return [NSApp isActive];
}

} // namespace s3g::clap_support
#endif
