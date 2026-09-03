#pragma once

#import <Cocoa/Cocoa.h>

#ifdef __cplusplus
#include "s3g/tracker/song_playback_planner.h"
#include "s3g/tracker/timing_warp.h"
#endif

NS_ASSUME_NONNULL_BEGIN

typedef void (^S3GTrackerSongDidChangeHandler)(NSString* summary);
typedef void (^S3GTrackerSongModeDidChangeHandler)(BOOL enabled);
typedef void (^S3GTrackerSongLoopDidChangeHandler)(BOOL enabled);
typedef void (^S3GTrackerSongLaunchHandler)(NSUInteger row,
    NSInteger quantization);
typedef void (^S3GTrackerSongProjectFileHandler)(void);

/// Retained, nonmodal editor for the in-memory song arrangement.
///
/// Prefer sharedController when the application has a single song window. The
/// controller owns its window and row model; closing the window only hides it,
/// so reopening it preserves every edit made during the application session.
@interface S3GTrackerSongWindowController : NSWindowController

+ (instancetype)sharedController;

/// Called on the main thread after a row, value, or lane mute changes.
@property(nonatomic, copy, nullable) S3GTrackerSongDidChangeHandler changeHandler;
@property(nonatomic, copy, nullable) S3GTrackerSongModeDidChangeHandler
    modeChangeHandler;
@property(nonatomic, copy, nullable) S3GTrackerSongLoopDidChangeHandler
    loopChangeHandler;
@property(nonatomic, copy, nullable) S3GTrackerSongLaunchHandler launchHandler;
/// Save/load handlers are owned by the host coordinator because the file is a
/// complete Tracker project: Song arrangement plus every referenced pattern.
@property(nonatomic, copy, nullable) S3GTrackerSongProjectFileHandler
    saveProjectHandler;
@property(nonatomic, copy, nullable) S3GTrackerSongProjectFileHandler
    loadProjectHandler;

/// Pattern mode remains the default. This explicit switch makes the main
/// transport consume Song rows on the next fresh Play.
@property(nonatomic) BOOL playbackEnabled;

/// A compact, human-readable description suitable for the main-window status
/// strip. This is derived from the current in-memory rows.
@property(nonatomic, readonly, copy) NSString* songSummary;

/// Shows the window without starting a modal session.
- (void)showWindow:(nullable id)sender;
- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    activePatternId:(NSString*)activePatternId;
- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    patternNames:(NSArray<NSString*>*)patternNames
    activePatternId:(NSString*)activePatternId;
- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    patternNames:(NSArray<NSString*>*)patternNames
    patternLengths:(NSArray<NSNumber*>*)patternLengths
    activePatternId:(NSString*)activePatternId;
- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    patternNames:(NSArray<NSString*>*)patternNames
    patternLengths:(NSArray<NSNumber*>*)patternLengths
    patternLaneCounts:(NSArray<NSNumber*>*)patternLaneCounts
    activePatternId:(NSString*)activePatternId;

#ifdef __cplusplus
/// Native model boundary used by project persistence and the scheduler.
- (s3g::tracker::SongArrangement)songArrangement;
- (void)setSongArrangement:(const s3g::tracker::SongArrangement&)arrangement;
- (void)setTimingWarpLibrary:
    (const s3g::tracker::TimingWarpLibrary&)library;
- (void)setPlaybackRow:(NSUInteger)row valid:(BOOL)valid;
- (void)setPendingPlaybackRow:(NSUInteger)row valid:(BOOL)valid;
- (void)setPendingPlaybackRow:(NSUInteger)row valid:(BOOL)valid
    quantization:(NSInteger)quantization;
- (void)setPlaybackLocked:(BOOL)locked;
#endif

@end

NS_ASSUME_NONNULL_END
