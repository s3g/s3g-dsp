#pragma once

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

// A retained, read-only reference generated from CommandEngine's shared help
// catalog. Closing the window hides it; its frame and scroll position survive
// reopening for the life of the application.
@interface S3GTrackerConsoleHelpWindowController : NSWindowController

+ (instancetype)sharedController;
- (void)showWindow:(nullable id)sender;

@end

NS_ASSUME_NONNULL_END
