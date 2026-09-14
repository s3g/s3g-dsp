#include "s3g_cocoa_gui.h"
#if defined(S3G_TRACKER_VSTGUI_PILOT)
#include "s3g_vstgui_foundation.h"
#include "s3g_tracker_scaled_view.h"
#include "s3g_tracker_reaper_keyboard.h"
#include "s3g_tracker_vstgui_pilot.h"
#endif
#import "s3g_song_window.h"
#import "s3g_tracker_controls.h"
#import "s3g_tracker_help_window.h"
#import "s3g_tracker_phrase_view.h"
#import "s3g_tracker_assemble_view.h"
#import "s3g_tracker_workspace.h"
#include "s3g_tracker_workspace_layout.h"
#if defined(S3G_TRACKER_PORTABLE_SHELL)
#include "s3g_tracker_shell_host.h"
using s3g::tracker::editor::ShellController;
using s3g::tracker::editor::ShellPage;
#endif

#include "s3g/tracker/atomic_project_store.h"
#include "s3g_tracker_clap_adapter.h"
#include "s3g/tracker/preview_sequencer.h"
#include "s3g/tracker/asset_pack.h"
#include "s3g/tracker/command.h"
#include "s3g/tracker/fx_catalog.h"
#include "s3g/tracker/midi_step_recorder.h"
#include "s3g/tracker/project_codec.h"
#include "s3g/tracker/project_history.h"
#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/clap_command_controller.h"
#include "s3g/tracker/runtime_pattern_plan.h"
#include "s3g/tracker/timing_playback_scheduler.h"
#include "s3g/tracker/visual_note_hit_mailbox.h"

#include <clap/clap.h>
#include <clap/ext/draft/transport-control.h>

#import <CoreText/CoreText.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

@class S3GTrackerClapCoordinator;

namespace {

using s3g::tracker::MidiStepCapture;
using s3g::tracker::MidiLiveRecordState;
using s3g::tracker::MidiStepRecordCode;
using s3g::tracker::MidiStepRecordMode;
using s3g::tracker::PatternBankEntry;
using s3g::tracker::PatternVariationLaunch;
using s3g::tracker::ProjectDocument;
using s3g::tracker::TrackerAssetPack;
using s3g::tracker::SongLaunchQuantization;
using s3g::tracker::SongArrangement;
using s3g::tracker::VisualNoteHitEvent;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;
using s3g::tracker::syncSessionToActivePattern;
using s3g::tracker::loadActivePatternIntoSession;
using s3g::tracker::syncActiveAssetBanks;
using s3g::tracker::loadActiveAssetBanks;

constexpr uint32_t kNativeWidth = 1320u;
constexpr uint32_t kNativeHeight = 860u;
constexpr uint32_t kMinimumWidth = 760u;
constexpr uint32_t kMinimumHeight = 620u;

#if defined(S3G_TRACKER_VSTGUI_PILOT)
bool adjustTrackerSize(uint32_t* width, uint32_t* height)
{
    using namespace s3g::portable_gui::foundation;
    if (!width || !height) return false;
    // Preserve an already rounded proportional size. Reapplying min(w/x,h/y)
    // can otherwise shed a pixel on every adjust_size / set_size round trip.
    const double sx = static_cast<double>(*width) / kNativeWidth;
    const double sy = static_cast<double>(*height) / kNativeHeight;
    if (sx >= kMinimumEditorScale && sx <= kMaximumEditorScale
        && sy >= kMinimumEditorScale && sy <= kMaximumEditorScale
        && std::abs(sx - sy) <= 0.5 / kNativeWidth + 0.5 / kNativeHeight)
        return true;
    return s3g::clap_gui::portable::adjustSize(
        kNativeWidth, kNativeHeight, width, height);
}
#endif


std::size_t longestPatternColumnLength(
    const s3g::tracker::Pattern& pattern) noexcept
{
    std::size_t longest = 1u;
    const auto include = [&](const s3g::tracker::ColumnDefinition& column) {
        longest = std::max(longest, column.length);
    };
    for (const auto& track : pattern.tracks) {
        include(track.noteColumn);
        include(track.instrumentColumn);
        include(track.velocityColumn);
        for (const auto& pair : track.fxPairs) {
            include(pair.actionColumn);
            include(pair.valueColumn);
        }
    }
    return std::clamp(longest, std::size_t { 1u },
        static_cast<std::size_t>(
            s3g::tracker::kMaximumSongPatternRows));
}

using s3g::tracker::refreshProjectBurstUsageCounts;

std::string nextPatternId(const s3g::tracker::PatternBank& bank)
{
    for (std::size_t index = 1u;
         index <= s3g::tracker::kMaximumPatternBankEntries; ++index) {
        std::string id = "A";
        if (index < 10u) id += "0";
        id += std::to_string(index);
        if (!bank.findEntry(id)) return id;
    }
    return {};
}

PatternBankEntry newPatternEntry(const PatternBankEntry& source,
    std::string id, bool duplicate)
{
    PatternBankEntry entry = source;
    entry.id = std::move(id);
    entry.pattern.name = duplicate
        ? source.pattern.name + " COPY" : "PATTERN " + entry.id;
    if (duplicate) return entry;
    constexpr std::size_t kBlankPatternRows = 64u;
    entry.pattern.visibleRows = kBlankPatternRows;
    for (auto& track : entry.pattern.tracks) {
        track.notes.assign(kBlankPatternRows,
            s3g::tracker::NoteCell::rest());
        track.instruments.assign(kBlankPatternRows,
            s3g::tracker::InstrumentCell::empty());
        track.velocities.assign(kBlankPatternRows,
            s3g::tracker::ValueCell::defaultValue());
        track.noteColumn = {};
        track.instrumentColumn = {};
        track.velocityColumn = {};
        track.noteColumn.length = kBlankPatternRows;
        track.instrumentColumn.length = kBlankPatternRows;
        track.velocityColumn.length = kBlankPatternRows;
        for (auto& pair : track.fxPairs) {
            pair.actions.assign(kBlankPatternRows,
                s3g::tracker::FxActionCell::empty());
            pair.values.assign(kBlankPatternRows,
                s3g::tracker::FxValueCell::previous());
            pair.actionColumn = {};
            pair.valueColumn = {};
            pair.actionColumn.length = kBlankPatternRows;
            pair.valueColumn.length = kBlankPatternRows;
        }
    }
    return entry;
}

PatternBankEntry variationPatternEntry(const PatternBankEntry& source,
    std::string id,
    const s3g::tracker::PatternVariationRequest& variation)
{
    PatternBankEntry entry = source;
    entry.id = std::move(id);
    entry.pattern = variation.generatedSession.pattern;
    entry.laneDefaultNotes = variation.generatedSession.laneDefaultNotes;
    entry.aliases = variation.generatedSession.aliases;
    entry.pattern.name = source.pattern.name.empty()
        ? "VAR " + entry.id
        : source.pattern.name + " VAR " + entry.id;
    return entry;
}



void registerBundledFonts()
{
    NSBundle* bundle = [NSBundle bundleForClass:
        NSClassFromString(@"S3GTrackerWorkspaceController")];
    constexpr std::array<const char*, 3u> names {{
        "IBMPlexMono-Regular", "IBMPlexMono-Medium",
        "IBMPlexMono-SemiBold",
    }};
    for (const char* name : names) {
        NSURL* url = [bundle URLForResource:[NSString stringWithUTF8String:name]
            withExtension:@"ttf" subdirectory:@"Fonts"];
        if (!url) continue;
        CFErrorRef error = nullptr;
        (void)CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url,
            kCTFontManagerScopeProcess, &error);
        if (error) CFRelease(error);
    }
}


struct Plugin : s3g::tracker::midi::Engine {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    S3GTrackerClapCoordinator* coordinator = nil;
    void* guiView = nullptr;
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    S3GTrackerClapScaledView* guiContainer = nil;
#else
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

// The audio callback commonly renders a block shortly before the downstream
// instrument presents it. Keep one 60 Hz GUI frame in hand so the playhead is
// not painted from the newly rendered block ahead of the audible onset.
struct VisualPlaybackFrame {
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        notePlayheads {};
    std::array<bool, s3g::tracker::kMaximumTrackCount> noteHits {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount> noteHitRows {};
    std::array<uint64_t, s3g::tracker::kMaximumTrackCount>
        noteHitSampleTimes {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        instrumentPlayheads {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        velocityPlayheads {};
    std::array<std::array<std::size_t, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxActionPlayheads {};
    std::array<std::array<std::size_t, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxValuePlayheads {};
    float subrowPhase = 0.0f;
    uint64_t timingWarpTick = 0u;
    int32_t songRow = -1;
    int32_t pendingSongRow = -1;
    uint32_t pendingSongQuantization = 0u;
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}


const clap_host_transport_control_t* hostTransportControl(Plugin& plugin)
{
    if (!plugin.host || !plugin.host->get_extension) return nullptr;
    return static_cast<const clap_host_transport_control_t*>(
        plugin.host->get_extension(plugin.host, CLAP_EXT_TRANSPORT_CONTROL));
}

// REAPER exposes this prefix of reaper_plugin_info_t to CLAP plug-ins through
// "cockos.reaper_extension". Keep the bridge local and minimal so Tracker can
// use the host's documented transport buttons without taking on the complete
// REAPER extension SDK.
struct ReaperHostBridge {
    int callerVersion;
    void* mainWindow;
    int (*registerObject)(const char*, void*);
    void* (*getFunction)(const char*);
};

const ReaperHostBridge* reaperHostBridge(Plugin& plugin)
{
    if (!plugin.host || !plugin.host->get_extension) return nullptr;
    return static_cast<const ReaperHostBridge*>(plugin.host->get_extension(
        plugin.host, "cockos.reaper_extension"));
}

using ReaperTransportButton = void (*)();
using ReaperPlayState = int (*)();
using ReaperMasterTempo = double (*)();

ReaperTransportButton reaperTransportButton(Plugin& plugin,
    const char* name)
{
    const auto* bridge = reaperHostBridge(plugin);
    if (!bridge || !bridge->getFunction) return nullptr;
    return reinterpret_cast<ReaperTransportButton>(
        bridge->getFunction(name));
}

std::optional<bool> reaperTransportIsPlaying(Plugin& plugin)
{
    const auto* bridge = reaperHostBridge(plugin);
    if (!bridge || !bridge->getFunction) return std::nullopt;
    const auto getPlayState = reinterpret_cast<ReaperPlayState>(
        bridge->getFunction("GetPlayState"));
    if (!getPlayState) return std::nullopt;
    const int state = getPlayState();
    return (state & 1) != 0 && (state & 2) == 0;
}

std::optional<double> reaperHostTempo(Plugin& plugin)
{
    const auto* bridge = reaperHostBridge(plugin);
    if (!bridge || !bridge->getFunction) return std::nullopt;
    const auto masterTempo = reinterpret_cast<ReaperMasterTempo>(
        bridge->getFunction("Master_GetTempo"));
    if (!masterTempo) return std::nullopt;
    const double tempo = masterTempo();
    return std::isfinite(tempo) && tempo > 0.0
        ? std::optional<double>(tempo) : std::nullopt;
}

bool requestHostContinue(Plugin& plugin)
{
    const auto* control = hostTransportControl(plugin);
    if (control && control->request_continue) {
        control->request_continue(plugin.host);
        return true;
    }
    if (const auto play = reaperTransportButton(plugin, "OnPlayButton")) {
        play();
        return true;
    }
    return false;
}

bool requestHostStop(Plugin& plugin)
{
    const auto* control = hostTransportControl(plugin);
    if (control && control->request_stop) {
        control->request_stop(plugin.host);
        return true;
    }
    if (const auto stop = reaperTransportButton(plugin, "OnStopButton")) {
        stop();
        return true;
    }
    return false;
}

bool requestHostTogglePlayback(Plugin& plugin)
{
    const auto* control = hostTransportControl(plugin);
    if (control && control->request_toggle_play) {
        control->request_toggle_play(plugin.host);
        return true;
    }
    const bool playing = reaperTransportIsPlaying(plugin).value_or(
        plugin.visualPlaying.load(std::memory_order_relaxed));
    if (control) {
        if (playing && control->request_pause) {
            control->request_pause(plugin.host);
            return true;
        }
        if (!playing && control->request_continue) {
            control->request_continue(plugin.host);
            return true;
        }
    }
    const auto button = reaperTransportButton(plugin,
        playing ? "OnPauseButton" : "OnPlayButton");
    if (!button) return false;
    button();
    return true;
}


} // namespace

typedef NS_ENUM(NSInteger, S3GTrackerClapPage) {
    S3GTrackerClapPageTracker = 0,
    S3GTrackerClapPageSong,
    S3GTrackerClapPageGeometry,
    S3GTrackerClapPageBursts,
    S3GTrackerClapPagePhrases,
    S3GTrackerClapPageAssemble,
    S3GTrackerClapPageReshape,
    S3GTrackerClapPageWarps,
    S3GTrackerClapPageConsole,
    S3GTrackerClapPageHelp,
};

@interface S3GTrackerClapPageView : NSView <NSWindowDelegate> {
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    ShellController _shell;
#endif
}
#if defined(S3G_TRACKER_PORTABLE_SHELL)
@property(nonatomic, strong) S3GTrackerShellHost* shellHost;
#endif
@property(nonatomic, copy) NSArray<NSView*>* pageViews;
@property(nonatomic, copy) NSArray<NSView*>* pageHosts;
@property(nonatomic, copy) NSArray<NSTextField*>* detachedPlaceholders;
@property(nonatomic, copy) NSArray<S3GTrackerActionButton*>* pageButtons;
@property(nonatomic, strong) S3GTrackerActionButton* popoutButton;
@property(nonatomic, strong) NSTextField* midiEventDisplay;
@property(nonatomic, strong) NSTextField* hostBpmDisplay;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, NSWindow*>*
    detachedWindows;
@property(nonatomic) S3GTrackerClapPage selectedPage;
- (instancetype)initWithPages:(NSArray<NSView*>*)pages;
- (void)showPage:(S3GTrackerClapPage)page;
- (void)setHostBpm:(double)bpm;
- (void)setMidiEventText:(NSString*)text;
- (BOOL)pageCanDetach:(S3GTrackerClapPage)page;
- (void)toggleDetachPage:(S3GTrackerClapPage)page;
- (void)reattachPage:(S3GTrackerClapPage)page closeWindow:(BOOL)closeWindow;
- (void)detachFromPlugin;
- (BOOL)navigatePageForEvent:(NSEvent*)event;
@end

@implementation S3GTrackerClapPageView

- (instancetype)initWithPages:(NSArray<NSView*>*)pages
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0,
        kNativeWidth, kNativeHeight)];
    if (!self) return nil;
    self.wantsLayer = YES;
    self.layer.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas).CGColor;
    self.accessibilityElement = YES;
    self.accessibilityRole = NSAccessibilityGroupRole;
    self.accessibilityLabel = @"s3g Tracker REAPER page workspace";
    self.pageViews = pages;
    self.detachedWindows = [[NSMutableDictionary alloc] init];

#if defined(S3G_TRACKER_PORTABLE_SHELL)
    s3g::tracker::editor::ShellServices services;
    __weak S3GTrackerClapPageView* weakSelf = self;
    services.selectPage = [weakSelf](ShellPage page, bool twice) {
        auto* owner = weakSelf;
        if (!owner) return;
        [owner showPage:static_cast<S3GTrackerClapPage>(page)];
        if (twice) {
            // Let VSTGUI finish the source mouse event before reparenting
            // CFrames and changing the key window.
            dispatch_async(dispatch_get_main_queue(), ^{
                auto* deferredOwner = weakSelf;
                if (deferredOwner.window && !deferredOwner.hiddenOrHasHiddenAncestor)
                    [deferredOwner toggleDetachPage:static_cast<S3GTrackerClapPage>(page)];
            });
        }
    };
    services.toggleDetach = [weakSelf](ShellPage page) {
        dispatch_async(dispatch_get_main_queue(), ^{
            auto* owner = weakSelf;
            if (owner.window && !owner.hiddenOrHasHiddenAncestor)
                [owner toggleDetachPage:static_cast<S3GTrackerClapPage>(page)];
        });
    };
    self.shellHost = [[S3GTrackerShellHost alloc] initWithModel:&_shell services:std::move(services)];
    if (!self.shellHost) return nil;
    [self addSubview:self.shellHost];
#else
    NSArray<NSString*>* titles = @[
        @"TRACKER", @"SONG", @"GEOMETRY", @"BURSTS", @"PHRASES",
        @"ASSEMBLE", @"RESHAPE", @"WARPS", @"CONSOLE", @"HELP",
    ];
    NSMutableArray<S3GTrackerActionButton*>* buttons =
        [[NSMutableArray alloc] initWithCapacity:titles.count];
    for (NSUInteger index = 0u; index < titles.count; ++index) {
        S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        button.title = titles[index];
        button.font = S3GTrackerFont(10.5, NSFontWeightMedium);
        button.target = self;
        button.action = @selector(pagePressed:);
        button.identifier = [NSString stringWithFormat:@"%lu",
            static_cast<unsigned long>(index)];
        button.accessibilityLabel = [titles[index]
            stringByAppendingString:@" page"];
        [self addSubview:button];
        [buttons addObject:button];
    }
    self.pageButtons = buttons;
#endif

    NSMutableArray<NSView*>* hosts = [[NSMutableArray alloc]
        initWithCapacity:self.pageViews.count];
    NSMutableArray<NSTextField*>* placeholders = [[NSMutableArray alloc]
        initWithCapacity:self.pageViews.count];
    for (NSUInteger index = 0u; index < self.pageViews.count; ++index) {
        NSView* host = [[NSView alloc] initWithFrame:NSZeroRect];
        host.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        host.wantsLayer = YES;
        host.layer.backgroundColor = S3GTrackerThemeColor(
            S3GTrackerThemeRole::Canvas).CGColor;
#if defined(S3G_TRACKER_PORTABLE_SHELL)
        [self addSubview:host positioned:NSWindowAbove relativeTo:self.shellHost];
#else
        [self addSubview:host positioned:NSWindowBelow relativeTo:nil];
        NSTextField* placeholder = [NSTextField labelWithString:
            @"THIS PAGE IS OPEN IN A DETACHED WINDOW"];
        placeholder.font = S3GTrackerFont(11.0, NSFontWeightMedium);
        placeholder.textColor = S3GTrackerThemeColor(
            S3GTrackerThemeRole::TextMuted);
        placeholder.alignment = NSTextAlignmentCenter;
        placeholder.hidden = YES;
        [host addSubview:placeholder];
        [placeholders addObject:placeholder];
#endif
        [hosts addObject:host];

        NSView* page = self.pageViews[index];
        [page removeFromSuperview];
        page.translatesAutoresizingMaskIntoConstraints = YES;
        page.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
#if defined(S3G_TRACKER_PORTABLE_MAIN_PAGE)
        if (index == static_cast<NSUInteger>(S3GTrackerClapPageTracker))
            page.autoresizingMask = NSViewNotSizable;
#endif
        [host addSubview:page];
    }
    self.pageHosts = hosts;
    self.detachedPlaceholders = placeholders;

#if !defined(S3G_TRACKER_PORTABLE_SHELL)
    self.popoutButton = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    self.popoutButton.s3gUsesSuiteStyle = YES;
    self.popoutButton.title = @"↗";
    self.popoutButton.font = S3GTrackerFont(13.0, NSFontWeightMedium);
    self.popoutButton.target = self;
    self.popoutButton.action = @selector(popoutPressed:);
    self.popoutButton.accessibilityLabel = @"Detach selected tool page";
    [self addSubview:self.popoutButton];
    self.midiEventDisplay = [NSTextField labelWithString:
        @"0 MIDI EVENTS  •  SEND 0  DROP 0  LATE 0  CLK 0"];
    self.midiEventDisplay.font = s3g::clap_gui::uiFont(8.5);
    self.midiEventDisplay.textColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::TextMuted);
    self.midiEventDisplay.alignment = NSTextAlignmentRight;
    self.midiEventDisplay.lineBreakMode = NSLineBreakByTruncatingHead;
    self.midiEventDisplay.accessibilityElement = YES;
    self.midiEventDisplay.accessibilityRole = NSAccessibilityStaticTextRole;
    self.midiEventDisplay.accessibilityLabel = @"MIDI event statistics";
    [self addSubview:self.midiEventDisplay];
    self.hostBpmDisplay = [NSTextField labelWithString:@"HOST BPM  —"];
    self.hostBpmDisplay.font = s3g::clap_gui::uiFont(10.0);
    self.hostBpmDisplay.textColor = s3g::clap_gui::color(0x929292);
    self.hostBpmDisplay.alignment = NSTextAlignmentRight;
    self.hostBpmDisplay.lineBreakMode = NSLineBreakByClipping;
    self.hostBpmDisplay.accessibilityElement = YES;
    self.hostBpmDisplay.accessibilityRole = NSAccessibilityStaticTextRole;
    self.hostBpmDisplay.accessibilityLabel =
        @"Host tempo in beats per minute";
    [self addSubview:self.hostBpmDisplay];
#endif
    [self showPage:S3GTrackerClapPageTracker];
    return self;
}

- (BOOL)isFlipped { return YES; }

#if defined(S3G_TRACKER_PORTABLE_SHELL)
- (void)dealloc
{
    // Destroy the portable view before its referenced C++ shell model.
    [self.shellHost removeFromSuperview];
    self.shellHost = nil;
}
#endif

- (BOOL)navigatePageForEvent:(NSEvent*)event
{
    const auto modifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption | NSEventModifierFlagShift);
    if (modifiers != NSEventModifierFlagShift) return NO;
    NSString* characters = event.characters;
    const BOOL previous = event.keyCode == 43u
        || [characters isEqualToString:@"<"];
    const BOOL next = event.keyCode == 47u
        || [characters isEqualToString:@">"];
    if (!previous && !next) return NO;

    NSResponder* responder = self.window.firstResponder;
    if ([responder isKindOfClass:NSTextView.class]
        || [NSStringFromClass(responder.class) containsString:@"MenuOverlay"])
        return NO;
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    // Shift-< / Shift-> are ordinary characters inside generic text editors.
    for (NSView* page in self.pageViews)
        if ([page conformsToProtocol:@protocol(S3GTrackerTextInputOwner)]
            && [(id<S3GTrackerTextInputOwner>)page s3gTrackerHasFocusedTextInput])
            return NO;
#endif

#if defined(S3G_TRACKER_PORTABLE_SHELL)
    [self showPage:static_cast<S3GTrackerClapPage>(_shell.adjacent(next))];
    return YES;
#else

    const NSInteger count = static_cast<NSInteger>(self.pageViews.count);
    if (count <= 0) return NO;
    NSInteger index = static_cast<NSInteger>(self.selectedPage)
        + (next ? 1 : -1);
    if (index < 0) index = count - 1;
    if (index >= count) index = 0;
    [self showPage:static_cast<S3GTrackerClapPage>(index)];
    return YES;
#endif
}

- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if ([self navigatePageForEvent:event]) return YES;
    if (self.selectedPage == S3GTrackerClapPagePhrases) {
        NSView* phrasePage = self.pageViews[
            static_cast<NSUInteger>(S3GTrackerClapPagePhrases)];
        if ([phrasePage conformsToProtocol:
                @protocol(S3GTrackerPhraseKeyHandling)]
            && [(id<S3GTrackerPhraseKeyHandling>)phrasePage
                s3gHandlePhraseKeyEquivalent:event]) return YES;
    }
    if (self.selectedPage == S3GTrackerClapPageAssemble) {
        NSView* page = self.pageViews[
            static_cast<NSUInteger>(S3GTrackerClapPageAssemble)];
        if ([page conformsToProtocol:@protocol(S3GTrackerAssembleKeyHandling)]
            && [(id<S3GTrackerAssembleKeyHandling>)page
                s3gHandleAssembleKeyEquivalent:event]) return YES;
    }
    return [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent*)event
{
    if ([self navigatePageForEvent:event]) return;
    if (self.selectedPage == S3GTrackerClapPagePhrases) {
        NSView* phrasePage = self.pageViews[
            static_cast<NSUInteger>(S3GTrackerClapPagePhrases)];
        if ([phrasePage conformsToProtocol:
                @protocol(S3GTrackerPhraseKeyHandling)]
            && [(id<S3GTrackerPhraseKeyHandling>)phrasePage
                s3gHandlePhraseKeyEquivalent:event]) return;
    }
    if (self.selectedPage == S3GTrackerClapPageAssemble) {
        NSView* page = self.pageViews[
            static_cast<NSUInteger>(S3GTrackerClapPageAssemble)];
        if ([page conformsToProtocol:@protocol(S3GTrackerAssembleKeyHandling)]
            && [(id<S3GTrackerAssembleKeyHandling>)page
                s3gHandleAssembleKeyEquivalent:event]) return;
    }
    [super keyDown:event];
}

- (void)layout
{
    [super layout];
    constexpr CGFloat navigationHeight = 40.0;
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    self.shellHost.frame = self.bounds;
    const auto layout = _shell.layout(NSWidth(self.bounds), NSHeight(self.bounds));
    const NSRect contentFrame = NSMakeRect(layout.content.x, layout.content.y,
        layout.content.width, layout.content.height);
#else
    CGFloat x = 12.0;
    const std::array<CGFloat, 10u> widths {{
        70.0, 50.0, 72.0, 60.0, 64.0, 72.0, 66.0, 54.0, 60.0, 46.0,
    }};
    for (NSUInteger index = 0u; index < self.pageButtons.count; ++index) {
        const CGFloat width = widths[std::min<std::size_t>(
            static_cast<std::size_t>(index), widths.size() - 1u)];
        self.pageButtons[index].frame = NSMakeRect(
            x, 6.0, width, navigationHeight - 12.0);
        x += width + 6.0;
    }
    self.popoutButton.frame = NSMakeRect(
        std::max<CGFloat>(12.0, NSWidth(self.bounds) - 48.0),
        6.0, 36.0, navigationHeight - 12.0);
    const CGFloat bpmRightInset = self.popoutButton.hidden ? 18.0 : 62.0;
    const CGFloat bpmWidth = 138.0;
    const CGFloat bpmX = std::max<CGFloat>(
        x + 8.0, NSWidth(self.bounds) - bpmRightInset - bpmWidth);
    self.hostBpmDisplay.frame = NSMakeRect(bpmX,
        10.0, bpmWidth, 20.0);
    const CGFloat eventX = x + 8.0;
    const CGFloat eventWidth = std::max<CGFloat>(0.0, bpmX - eventX - 10.0);
    self.midiEventDisplay.hidden = eventWidth < 40.0;
    self.midiEventDisplay.frame = NSMakeRect(eventX, 10.0,
        eventWidth, 20.0);
    const NSRect contentFrame = NSMakeRect(0.0, navigationHeight,
        NSWidth(self.bounds), std::max<CGFloat>(0.0,
            NSHeight(self.bounds) - navigationHeight));
#endif
    for (NSUInteger index = 0u; index < self.pageHosts.count; ++index) {
        NSView* host = self.pageHosts[index];
        host.frame = contentFrame;
#if !defined(S3G_TRACKER_PORTABLE_SHELL)
        self.detachedPlaceholders[index].frame = NSMakeRect(20.0,
            std::max<CGFloat>(20.0, NSMidY(host.bounds) - 10.0),
            std::max<CGFloat>(1.0, NSWidth(host.bounds) - 40.0), 20.0);
#endif
        if (!self.detachedWindows[@(index)]) {
#if defined(S3G_TRACKER_PORTABLE_MAIN_PAGE)
            // AppKit can round a magnified native host to fractional logical
            // dimensions during attachment. The VSTGUI page is a fixed
            // canvas; only the outer scroll view owns whole-interface zoom.
            if (index == static_cast<NSUInteger>(S3GTrackerClapPageTracker)) {
                self.pageViews[index].frame = NSMakeRect(0.0, 0.0,
                    kNativeWidth, kNativeHeight - navigationHeight);
                continue;
            }
#endif
            self.pageViews[index].frame = host.bounds;
        }
    }
}

- (void)setHostBpm:(double)bpm
{
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    _shell.setHostBpm(bpm);
    [self.shellHost refresh];
#else
    NSString* text = bpm > 0.0
        ? [NSString stringWithFormat:@"HOST BPM  %.2f", bpm]
        : @"HOST BPM  —";
    if (![self.hostBpmDisplay.stringValue isEqualToString:text])
        self.hostBpmDisplay.stringValue = text;
    self.hostBpmDisplay.accessibilityValue = text;
#endif
}

- (void)setMidiEventText:(NSString*)text
{
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    _shell.setEventText(text.UTF8String ?: "");
    [self.shellHost refresh];
#else
    NSString* display = text.length > 0u ? text : @"0 MIDI EVENTS";
    if (![self.midiEventDisplay.stringValue isEqualToString:display])
        self.midiEventDisplay.stringValue = display;
    self.midiEventDisplay.toolTip = display;
    self.midiEventDisplay.accessibilityValue = display;
#endif
}

- (void)pagePressed:(NSButton*)sender
{
    const auto page = static_cast<S3GTrackerClapPage>(
        sender.identifier.integerValue);
    [self showPage:page];
    if (NSApp.currentEvent.clickCount >= 2 && [self pageCanDetach:page]) {
        [self toggleDetachPage:page];
    }
}

- (BOOL)pageCanDetach:(S3GTrackerClapPage)page
{
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    return ShellController::canDetach(static_cast<ShellPage>(page));
#else
    return page == S3GTrackerClapPageGeometry
        || page == S3GTrackerClapPageBursts
        || page == S3GTrackerClapPagePhrases
        || page == S3GTrackerClapPageAssemble
        || page == S3GTrackerClapPageReshape
        || page == S3GTrackerClapPageWarps
        || page == S3GTrackerClapPageConsole
        || page == S3GTrackerClapPageHelp;
#endif
}

- (void)popoutPressed:(id)sender
{
    (void)sender;
    [self toggleDetachPage:self.selectedPage];
}

- (void)toggleDetachPage:(S3GTrackerClapPage)page
{
    if (![self pageCanDetach:page]) return;
    NSNumber* key = @(static_cast<NSInteger>(page));
    NSWindow* existing = self.detachedWindows[key];
    if (existing) {
        [self reattachPage:page closeWindow:YES];
        return;
    }
    const NSInteger index = static_cast<NSInteger>(page);
    if (index < 0 || index >= static_cast<NSInteger>(self.pageViews.count))
        return;
    NSWindow* parentWindow = self.window;
    NSView* content = self.pageViews[static_cast<NSUInteger>(index)];
    [content removeFromSuperview];
    NSWindow* window = [[NSWindow alloc] initWithContentRect:
        NSMakeRect(0.0, 0.0, 920.0, 660.0)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    NSArray<NSString*>* names = @[
        @"Tracker", @"Song", @"Rhythm Geometry", @"Bursts",
        @"MIDI Phrases", @"Phrase Assembly", @"Pattern Reshape",
        @"Timing Warps", @"Console", @"Help",
    ];
    window.title = [@"s3g Tracker — " stringByAppendingString:
        names[static_cast<NSUInteger>(index)]];
    window.minSize = NSMakeSize(480.0, 360.0);
    window.backgroundColor = S3GTrackerThemeColor(
        S3GTrackerThemeRole::Canvas);
    window.appearance = [NSAppearance appearanceNamed:
        NSAppearanceNameDarkAqua];
    window.releasedWhenClosed = NO;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    // Lifetime is owned by the page view, but the pop-out must not be an
    // NSWindow child of REAPER's plug-in window. Child windows are tied to
    // their parent's display Space and disappear when moved to another
    // monitor. This matches the independent pop-out rule used by No Input
    // Mixer while detachFromPlugin still handles the CLAP GUI lifecycle.
    window.hidesOnDeactivate = NO;
    window.delegate = self;
    content.frame = window.contentView.bounds;
    content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window.contentView = content;
    self.detachedWindows[key] = window;
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    _shell.setDetached(static_cast<ShellPage>(page), true);
#else
    self.detachedPlaceholders[static_cast<NSUInteger>(index)].hidden = NO;
#endif
    window.level = parentWindow
        ? std::max<NSInteger>(
            NSFloatingWindowLevel, parentWindow.level + 1)
        : NSFloatingWindowLevel;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [self showPage:page];
}

- (void)reattachPage:(S3GTrackerClapPage)page closeWindow:(BOOL)closeWindow
{
    NSNumber* key = @(static_cast<NSInteger>(page));
    NSWindow* window = self.detachedWindows[key];
    if (!window) return;
    const NSUInteger index = static_cast<NSUInteger>(page);
    NSView* content = self.pageViews[index];
    window.delegate = nil;
    if (window.parentWindow)
        [window.parentWindow removeChildWindow:window];
    window.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
    [content removeFromSuperview];
    [self.pageHosts[index] addSubview:content];
    content.frame = self.pageHosts[index].bounds;
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    _shell.setDetached(static_cast<ShellPage>(page), false);
#else
    self.detachedPlaceholders[index].hidden = YES;
#endif
    [self.detachedWindows removeObjectForKey:key];
    if (closeWindow) [window close];
    else [window orderOut:nil];
    [self showPage:page];
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    for (NSNumber* key in self.detachedWindows.allKeys) {
        if (self.detachedWindows[key] != sender) continue;
        [self reattachPage:static_cast<S3GTrackerClapPage>(
            key.integerValue) closeWindow:NO];
        return NO;
    }
    return YES;
}

- (void)detachFromPlugin
{
    for (NSNumber* key in self.detachedWindows.allKeys.copy)
        [self reattachPage:static_cast<S3GTrackerClapPage>(
            key.integerValue) closeWindow:YES];
}

- (void)showPage:(S3GTrackerClapPage)page
{
    const NSInteger index = static_cast<NSInteger>(page);
    if (index < 0 || index >= static_cast<NSInteger>(self.pageViews.count))
        return;
    self.selectedPage = page;
#if defined(S3G_TRACKER_PORTABLE_SHELL)
    _shell.select(static_cast<ShellPage>(page));
    [self.shellHost refresh];
#endif
    for (NSUInteger item = 0u; item < self.pageHosts.count; ++item) {
        const BOOL selected = item == static_cast<NSUInteger>(index);
#if defined(S3G_TRACKER_PORTABLE_SHELL)
        self.pageHosts[item].hidden = !selected || _shell.detached(ShellPage(item));
#else
        self.pageHosts[item].hidden = !selected;
#endif
        self.pageHosts[item].accessibilityHidden = !selected;
        self.pageButtons[item].state = selected
            ? NSControlStateValueOn : NSControlStateValueOff;
        self.pageButtons[item].tag = selected ? 1 : 0;
        [self.pageButtons[item] setNeedsDisplay:YES];
    }
    const BOOL detachable = [self pageCanDetach:page];
    self.popoutButton.hidden = !detachable;
    const BOOL detached = self.detachedWindows[@(index)] != nil;
    self.popoutButton.title = detached ? @"↙" : @"↗";
    self.popoutButton.toolTip = detached
        ? @"Return this tool to the plug-in window"
        : @"Open this tool in its own window";
    if (detached)
        [self.detachedWindows[@(index)] makeKeyAndOrderFront:nil];
    [self setNeedsLayout:YES];
    NSAccessibilityPostNotification(
        self, NSAccessibilityLayoutChangedNotification);
}

@end

@interface S3GTrackerClapCoordinator : NSObject {
@private
    Plugin* _plugin;
    std::unique_ptr<TrackerViewState> _state;
    std::unique_ptr<WorkspaceCallbacks> _callbacks;
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    std::unique_ptr<s3g::tracker::editor::MacReaperTextInput> _reaperTextInput;
#endif
    std::unique_ptr<s3g::tracker::ClapDocumentController> _documentController;
    std::array<uint64_t, s3g::tracker::kMaximumTrackCount>
        _consumedNoteHitSequences;
    VisualPlaybackFrame _pendingVisualFrame;
    bool _visualFramePrimed;
    uint64_t _reportedStepRecordDrops;
    MidiLiveRecordState _midiLiveRecordState;
    bool _runtimePublicationPending;
    bool _deferredSongRuntimePublication;
    bool _transportWasPlaying;
    bool _playingSongArrangementValid;
    SongArrangement _playingSongArrangement;
}
@property(nonatomic, strong) S3GTrackerWorkspaceController* workspace;
@property(nonatomic, strong) S3GTrackerSongWindowController* songWindow;
@property(nonatomic, strong) S3GTrackerConsoleHelpWindowController* helpWindow;
@property(nonatomic, strong) S3GTrackerClapPageView* pageView;
@property(nonatomic, strong) NSTimer* displayTimer;
@property(nonatomic, strong) NSTimer* runtimePublicationTimer;
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (ProjectDocument)currentDocument;
- (void)applyDocument:(const ProjectDocument&)document;
- (void)updateHistoryAvailability;
- (void)resetHistory:(const ProjectDocument&)document;
- (void)recordHistory:(const ProjectDocument&)document;
- (void)undoProject;
- (void)redoProject;
- (void)consumeMidiStepCaptures;
- (void)scheduleRuntimePublication;
- (void)cancelRuntimePublication;
- (void)flushRuntimePublication;
- (void)disarmMidiStepRecording;
- (void)updateMidiMonitorChannel;
- (void)commitProjectWithoutRuntime:(BOOL)dirty;
- (void)commitSongProjectEdit:(BOOL)dirty;
- (void)presentSaveSongProject;
- (void)presentLoadSongProject;
- (void)presentImportAssetPack;
- (void)presentExportAssetPack:(const TrackerAssetPack&)pack;
- (BOOL)installPatternVariation:
    (const s3g::tracker::PatternVariationRequest&)variation;
- (void)startTimer;
- (void)stopTimer;
@end

@implementation S3GTrackerClapCoordinator

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super init];
    if (!self) return nil;
    _plugin = plugin;
    registerBundledFonts();
    _state = std::make_unique<TrackerViewState>();
    _documentController = std::make_unique<s3g::tracker::ClapDocumentController>(*_state);
    _callbacks = std::make_unique<WorkspaceCallbacks>();
    _consumedNoteHitSequences.fill(0u);
    _visualFramePrimed = false;
    _reportedStepRecordDrops = 0u;
    _runtimePublicationPending = false;
    _deferredSongRuntimePublication = false;
    _transportWasPlaying = false;
    _playingSongArrangementValid = false;
    _state->midiStepInputAvailable = true;
    _state->midiStepRecordMode = MidiStepRecordMode::Off;
    _state->midiRecordTrack = 0u;
    _state->fillActive = _plugin->fillActive.load(
        std::memory_order_acquire);
    _plugin->midiStepRecordMode.store(
        static_cast<uint8_t>(MidiStepRecordMode::Off),
        std::memory_order_relaxed);
    _plugin->midiRecordTrack.store(0u, std::memory_order_relaxed);

    __weak S3GTrackerClapCoordinator* weakSelf = self;
    _callbacks->togglePlayback = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        if (!requestHostTogglePlayback(*owner->_plugin)) {
            owner->_state->status = "Use REAPER transport controls";
            [owner.workspace reloadModel];
        }
    };
    _callbacks->restartPlayback = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->requestRestart.store(true, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        [owner.workspace appendConsoleMessage:
            "SYNC ALL queued for row 1; phase offsets ignored and REAPER transport unchanged"
            error:NO];
    };
    _callbacks->resyncTrack = [weakSelf](std::size_t track) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || track >= s3g::tracker::kMaximumTrackCount) return;
        owner->_plugin->requestTrackResyncMask.fetch_or(
            uint32_t { 1u } << track, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        [owner.workspace appendConsoleMessage:[NSString stringWithFormat:
            @"Lane %lu SYNC queued for next tick; REAPER transport unchanged",
            static_cast<unsigned long>(track + 1u)].UTF8String
            error:NO];
    };
    _callbacks->panic = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->requestPanic.store(true, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        [owner.workspace appendConsoleMessage:
            "MIDI panic queued on channels 1–16" error:NO];
    };
    _callbacks->previewBurst = [weakSelf](
        const s3g::tracker::BurstDefinition& burst, uint8_t midiChannel,
        double bpm, uint32_t ticksPerBeat) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || burst.empty()) return;
        const bool transportRunning = reaperTransportIsPlaying(
            *owner->_plugin).value_or(owner->_state->playing);
        if (transportRunning) {
            owner->_state->status =
                "Burst Preview is available while REAPER is stopped";
            [owner.workspace reloadModel];
            return;
        }
        const double projectBpm = reaperHostTempo(*owner->_plugin)
            .value_or(bpm);
        owner->_plugin->pitchPreview.cancel();
        owner->_plugin->burstPreviewMailbox.publish(
            burst, midiChannel, projectBpm, ticksPerBeat);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
    };
    _callbacks->previewPitchSequence = [weakSelf](
        const std::vector<s3g::tracker::PitchPreviewEvent>& events,
        uint8_t midiChannel, double bpm, uint32_t ticksPerBeat) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || events.empty()) return;
        const bool transportRunning = reaperTransportIsPlaying(
            *owner->_plugin).value_or(owner->_state->playing);
        if (transportRunning) {
            owner->_state->status =
                "Pitch Map Preview is available while REAPER is stopped";
            [owner.workspace reloadModel];
            return;
        }
        const double projectBpm = reaperHostTempo(*owner->_plugin)
            .value_or(bpm);
        s3g::tracker::BurstDefinition emptyBurst;
        owner->_plugin->burstPreviewMailbox.publish(
            emptyBurst, midiChannel, projectBpm, ticksPerBeat);
        owner->_plugin->pitchPreview.publish(
            events, midiChannel, projectBpm, ticksPerBeat);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
    };
    _callbacks->startAuthoringPreview = [weakSelf](
        const std::vector<s3g::tracker::PitchPreviewEvent>& events,
        uint8_t channel, double bpm, uint32_t ticks, uint32_t rows,
        bool loop) -> uint32_t {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || events.empty() || reaperTransportIsPlaying(
                *owner->_plugin).value_or(owner->_state->playing)) return 0;
        const double tempo = reaperHostTempo(*owner->_plugin).value_or(bpm);
        owner->_plugin->burstPreviewMailbox.publish({}, channel, tempo, ticks);
        const auto token = owner->_plugin->pitchPreview.publish(
            events, channel, tempo, ticks, rows, loop, true);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        return token;
    };
    _callbacks->stopAuthoringPreview = [weakSelf](uint32_t token) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->pitchPreview.cancel(token);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
    };
    _callbacks->authoringPreviewPosition = [weakSelf](uint32_t token) -> int64_t {
        S3GTrackerClapCoordinator* owner = weakSelf;
        return owner ? owner->_plugin->pitchPreview.position(token) : -1;
    };
    _callbacks->loopAuthoringPreview = [weakSelf](uint32_t token, bool loop) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (owner) owner->_plugin->pitchPreview.setLoop(token, loop);
    };
    _callbacks->showSongWindow = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageSong];
    };
    _callbacks->showGeometryPage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageGeometry];
    };
    _callbacks->showBurstPage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageBursts];
    };
    _callbacks->showReshapePage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageReshape];
    };
    _callbacks->showPhrasePage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPagePhrases];
    };
    _callbacks->showAssemblePage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageAssemble];
    };
    _callbacks->importAssetPack = [weakSelf] {
        [weakSelf presentImportAssetPack];
    };
    _callbacks->exportBurstAssetPack = [weakSelf](std::size_t slot) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state
            || slot >= owner->_state->session.burstLibrary.bursts.size())
            return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto& burst = owner->_state->session.burstLibrary.bursts[slot];
        [owner presentExportAssetPack:s3g::tracker::makeBurstAssetPack(
            burst.name.empty() ? "BURST PACK" : burst.name,
            owner->_state->session.burstLibrary, slot)];
    };
    _callbacks->exportBurstLibraryAssetPack = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state) return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* bank = s3g::tracker::findBurstBank(
            owner->_state->burstBanks, owner->_state->activeBurstBankId);
        const std::string name = bank && !bank->name.empty()
            ? bank->name : "BURST LIBRARY PACK";
        [owner presentExportAssetPack:s3g::tracker::makeBurstLibraryAssetPack(
            name, owner->_state->session.burstLibrary)];
    };
    _callbacks->exportPhraseAssetPack = [weakSelf](std::size_t slot) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state
            || slot >= owner->_state->phraseLibrary.phrases.size()) return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto& phrase = owner->_state->phraseLibrary.phrases[slot];
        [owner presentExportAssetPack:s3g::tracker::makePhraseAssetPack(
            phrase.name.empty() ? "PHRASE PACK" : phrase.name,
            owner->_state->phraseLibrary, slot,
            owner->_state->burstBanks)];
    };
    _callbacks->exportPhraseLibraryAssetPack = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state) return;
        (void)syncActiveAssetBanks(*owner->_state);
        [owner presentExportAssetPack:
            s3g::tracker::makePhraseLibraryAssetPack(
                "PHRASE LIBRARY PACK", owner->_state->phraseLibrary,
                owner->_state->burstBanks)];
    };
    _callbacks->copyBurstToProject = [weakSelf](std::size_t sourceSlot) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state) return s3g::tracker::kBurstDefinitionCount;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* sourceBank = s3g::tracker::findBurstBank(
            owner->_state->burstBanks, owner->_state->activeBurstBankId);
        auto* projectBank = s3g::tracker::findBurstBank(
            owner->_state->burstBanks, s3g::tracker::kProjectAssetBankId);
        if (!sourceBank || !projectBank
            || sourceSlot >= sourceBank->library.bursts.size()
            || sourceBank->library.bursts[sourceSlot].empty())
            return s3g::tracker::kBurstDefinitionCount;
        const auto empty = std::find_if(projectBank->library.bursts.begin(),
            projectBank->library.bursts.end(), [](const auto& definition) {
                return definition.empty();
            });
        if (empty == projectBank->library.bursts.end()) {
            owner->_state->status = "Project Burst bank is full";
            [owner.workspace reloadModel];
            return s3g::tracker::kBurstDefinitionCount;
        }
        const auto destination = static_cast<std::size_t>(
            empty - projectBank->library.bursts.begin());
        *empty = sourceBank->library.bursts[sourceSlot];
        if (sourceBank->id == s3g::tracker::kProjectAssetBankId)
            empty->name += " COPY";
        owner->_state->activeBurstBankId = s3g::tracker::kProjectAssetBankId;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        [owner commitProject:YES];
        [owner.workspace reloadModel];
        return destination;
    };
    _callbacks->copyPhraseToProject = [weakSelf](std::size_t sourceSlot) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state) return s3g::tracker::kPhraseLibrarySlots;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* sourceBank = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, owner->_state->activePhraseBankId);
        auto* projectBank = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, s3g::tracker::kProjectAssetBankId);
        if (!sourceBank || !projectBank
            || sourceSlot >= sourceBank->library.phrases.size())
            return s3g::tracker::kPhraseLibrarySlots;
        const auto& source = sourceBank->library.phrases[sourceSlot];
        if (source.empty() && source.name.empty())
            return s3g::tracker::kPhraseLibrarySlots;
        const auto empty = std::find_if(projectBank->library.phrases.begin(),
            projectBank->library.phrases.end(), [](const auto& definition) {
                return definition.empty() && definition.name.empty();
            });
        if (empty == projectBank->library.phrases.end()) {
            owner->_state->status = "Project Phrase bank is full";
            [owner.workspace reloadModel];
            return s3g::tracker::kPhraseLibrarySlots;
        }
        const auto destination = static_cast<std::size_t>(
            empty - projectBank->library.phrases.begin());
        *empty = source;
        if (sourceBank->id == s3g::tracker::kProjectAssetBankId)
            empty->name += " COPY";
        owner->_state->activePhraseBankId = s3g::tracker::kProjectAssetBankId;
        owner->_state->activeBurstBankId = s3g::tracker::kProjectAssetBankId;
        owner->_state->selectedPhrase = destination;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        [owner commitProject:YES];
        [owner.workspace reloadModel];
        return destination;
    };
    _callbacks->selectBurstBank = [weakSelf](s3g::tracker::AssetBankId id) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state || id == owner->_state->activeBurstBankId)
            return;
        (void)syncActiveAssetBanks(*owner->_state);
        if (!s3g::tracker::findBurstBank(owner->_state->burstBanks, id)) return;
        owner->_state->activeBurstBankId = id;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        [owner commitProject:YES];
        [owner.workspace reloadModel];
    };
    _callbacks->selectPhraseBank = [weakSelf](s3g::tracker::AssetBankId id) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state || id == owner->_state->activePhraseBankId)
            return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* phrase = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, id);
        if (!phrase) return;
        owner->_state->activePhraseBankId = id;
        owner->_state->activeBurstBankId = phrase->companionBurstBankId;
        owner->_state->selectedPhrase = 0u;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        [owner commitProject:YES];
        [owner.workspace reloadModel];
    };
    _callbacks->clearPhraseBank = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state) return;
        owner->_state->phraseLibrary = {};
        owner->_state->selectedPhrase = 0u;
        [owner commitProject:YES];
        [owner.workspace reloadModel];
    };
    _callbacks->deletePhraseBank = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state
            || owner->_state->activePhraseBankId
                == s3g::tracker::kProjectAssetBankId) return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto id = owner->_state->activePhraseBankId;
        owner->_state->phraseBanks.erase(std::remove_if(
            owner->_state->phraseBanks.begin(), owner->_state->phraseBanks.end(),
            [id](const s3g::tracker::PhraseBank& bank) { return bank.id == id; }),
            owner->_state->phraseBanks.end());
        owner->_state->activePhraseBankId = s3g::tracker::kProjectAssetBankId;
        const auto* project = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, s3g::tracker::kProjectAssetBankId);
        owner->_state->activeBurstBankId = project
            ? project->companionBurstBankId
            : s3g::tracker::kProjectAssetBankId;
        owner->_state->selectedPhrase = 0u;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        [owner commitProject:YES];
        [owner.workspace reloadModel];
    };
    _callbacks->deleteUnusedBursts = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state) return;
        refreshProjectBurstUsageCounts(*owner->_state);
        for (std::size_t slot = 0u;
             slot < owner->_state->session.burstLibrary.bursts.size(); ++slot)
            if (owner->_state->session.projectBurstUsageCounts[slot] == 0u)
                owner->_state->session.burstLibrary.bursts[slot] = {};
        [owner commitProject:YES];
        [owner.workspace reloadModel];
    };
    _callbacks->deleteBurstBank = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state
            || owner->_state->activeBurstBankId
                == s3g::tracker::kProjectAssetBankId) return;
        refreshProjectBurstUsageCounts(*owner->_state);
        const auto id = owner->_state->activeBurstBankId;
        const bool inUse = std::any_of(
            owner->_state->session.projectBurstUsageCounts.begin(),
            owner->_state->session.projectBurstUsageCounts.end(),
            [](std::size_t count) { return count != 0u; });
        const bool isCompanion = std::any_of(owner->_state->phraseBanks.begin(),
            owner->_state->phraseBanks.end(), [id](const auto& bank) {
                return bank.companionBurstBankId == id;
            });
        if (inUse || isCompanion) {
            owner->_state->status = inUse
                ? "Burst bank is still referenced by Patterns or Phrases"
                : "Delete its companion Phrase bank before deleting this Burst bank";
            [owner.workspace reloadModel];
            return;
        }
        (void)syncActiveAssetBanks(*owner->_state);
        owner->_state->burstBanks.erase(std::remove_if(
            owner->_state->burstBanks.begin(), owner->_state->burstBanks.end(),
            [id](const auto& bank) { return bank.id == id; }),
            owner->_state->burstBanks.end());
        owner->_state->activeBurstBankId = s3g::tracker::kProjectAssetBankId;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        [owner commitProject:YES];
        [owner.workspace reloadModel];
    };
    _callbacks->showTrackerPage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageTracker];
        [weakSelf.workspace focusTracker];
    };
    _callbacks->showWarpPage = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageWarps];
    };
    _callbacks->previewPattern = [weakSelf](
        const s3g::tracker::Pattern& pattern) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        [owner cancelRuntimePublication];
        ProjectDocument document = [owner currentDocument];
        auto* entry = document.patternBank.findEntry(
            document.patternBank.activePatternId);
        if (!entry) return;
        entry->pattern = pattern;
        if (!publishPreviewDocumentRuntime(*owner->_plugin,
                std::move(document))) {
            owner->_state->status = "Pattern reshape preview failed";
            [owner.workspace reloadModel];
        }
    };
    _callbacks->clearPatternPreview = [weakSelf] {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        [owner cancelRuntimePublication];
        if (!publishStoredDocumentRuntime(*owner->_plugin)) {
            owner->_state->status = "Could not restore stored pattern";
            [owner.workspace reloadModel];
        }
    };
    _callbacks->createPatternVariant = [weakSelf](
        const s3g::tracker::Pattern& pattern) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        [owner cancelRuntimePublication];
        if (owner->_state->patternBank.entries.size()
                >= s3g::tracker::kMaximumPatternBankEntries
            || !syncSessionToActivePattern(*owner->_state)) {
            owner->_state->status = "Pattern bank is full; variant not created";
            [owner.workspace reloadModel];
            return;
        }
        const std::string sourceId = owner->_state->patternBank.activePatternId;
        const auto* source = owner->_state->patternBank.findEntry(sourceId);
        const std::string id = nextPatternId(owner->_state->patternBank);
        if (!source || id.empty()) return;
        auto entry = newPatternEntry(*source, id, true);
        entry.pattern = pattern;
        entry.pattern.name = source->pattern.name.empty()
            ? "VAR " + id : source->pattern.name + " VAR " + id;
        owner->_state->patternBank.entries.push_back(std::move(entry));
        owner->_state->patternBank.activePatternId = id;
        if (!loadActivePatternIntoSession(*owner->_state)) return;
        owner->_state->session.selectedRow = 0u;
        owner->_state->status = "Created and selected variation " + id;
        [owner refreshSongPatterns];
        [owner commitProject:YES];
    };
    _callbacks->showConsoleHelp = [weakSelf] {
        [weakSelf.pageView showPage:S3GTrackerClapPageHelp];
    };
    _callbacks->instrumentRackChanged = [weakSelf] {
        [weakSelf commitProjectWithoutRuntime:YES];
    };
    _callbacks->instrumentRackReloaded = [weakSelf] {
        [weakSelf commitProject:NO];
    };
    _callbacks->reportError = [weakSelf](const std::string& message) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_state->status = message;
        [owner.workspace appendConsoleMessage:message error:YES];
    };
    _callbacks->selectionChanged = [] {
        // Editing selection is intentionally independent from REC LANE.
    };
    _callbacks->patternChanged = [weakSelf] {
        [weakSelf commitProject:YES];
    };
    _callbacks->selectPattern = [weakSelf](const std::string& id) {
        [weakSelf selectPattern:id];
    };
    _callbacks->addPattern = [weakSelf](bool duplicate) {
        [weakSelf addPattern:duplicate];
    };
    _callbacks->renamePattern = [weakSelf] {
        [weakSelf renamePattern];
    };
    _callbacks->deletePattern = [weakSelf] {
        [weakSelf deletePattern];
    };
    _callbacks->transportChanged = [weakSelf] {
        [weakSelf refreshSongWarps];
        [weakSelf commitProject:YES];
    };
    _callbacks->fillChanged = [weakSelf](bool active) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->fillActive.store(active, std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
    };
    _callbacks->outputChanged = [weakSelf] {
        [weakSelf commitProject:YES];
    };
    _callbacks->mainOutputGainChanged = [weakSelf](float) {
        [weakSelf commitProjectWithoutRuntime:YES];
    };
    _callbacks->viewPreferencesChanged = [weakSelf] {
        [weakSelf commitProjectWithoutRuntime:YES];
    };
    _callbacks->midiStepRecordModeChanged = [weakSelf](
        MidiStepRecordMode mode) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        [owner updateMidiMonitorChannel];
        const auto previous = static_cast<MidiStepRecordMode>(
            owner->_plugin->midiStepRecordMode.exchange(
                static_cast<uint8_t>(mode), std::memory_order_acq_rel));
        if (mode != previous) owner->_midiLiveRecordState.clear();
        if (mode == MidiStepRecordMode::Off
            && previous != MidiStepRecordMode::Off) {
            owner->_plugin->requestMidiMonitorRelease.store(
                true, std::memory_order_release);
            if (owner->_plugin->host
                && owner->_plugin->host->request_process)
                owner->_plugin->host->request_process(owner->_plugin->host);
        }
    };
    _callbacks->midiRecordTrackChanged = [weakSelf](std::size_t track) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || !owner->_state
            || owner->_state->session.pattern.tracks.empty()) return;
        owner->_state->midiRecordTrack = std::min(track,
            owner->_state->session.pattern.tracks.size() - 1u);
        owner->_plugin->midiRecordTrack.store(static_cast<uint32_t>(
            owner->_state->midiRecordTrack), std::memory_order_release);
        [owner updateMidiMonitorChannel];
    };
    _callbacks->tracksReordered = [weakSelf](const std::string& patternId,
        std::size_t source, std::size_t destination) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        NSString* pattern = [NSString stringWithUTF8String:patternId.c_str()];
        [owner.songWindow moveMutedLaneFrom:source to:destination
            patternId:pattern ? pattern : @""];
    };
    _callbacks->executeCommand = [weakSelf](const std::string& command) {
        [weakSelf executeCommand:command];
    };

    self.workspace = [[S3GTrackerWorkspaceController alloc]
        initWithState:_state.get() callbacks:_callbacks.get()];
    self.songWindow = [[S3GTrackerSongWindowController alloc] init];
    self.helpWindow = [[S3GTrackerConsoleHelpWindowController alloc] init];

    ProjectDocument initial;
    {
        std::lock_guard<std::mutex> lock(_plugin->documentMutex);
        initial = _plugin->document;
    }
    [self applyDocument:initial];
    [self resetHistory:[self currentDocument]];

    self.pageView = [[S3GTrackerClapPageView alloc] initWithPages:@[
        [self.workspace mainPageView],
        self.songWindow.window.contentView,
        [self.workspace geometryPageView],
        [self.workspace burstPageView],
        [self.workspace phrasePageView],
        [self.workspace assemblePageView],
        [self.workspace reshapePageView],
        [self.workspace warpPageView],
        [self.workspace consolePageView],
        self.helpWindow.window.contentView,
    ]];
    [self.pageView setHostBpm:_state->hostBpm];
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    if (const auto* bridge = reaperHostBridge(*plugin))
        _reaperTextInput = std::make_unique<s3g::tracker::editor::MacReaperTextInput>(
            bridge->registerObject, self.pageView.pageViews);
#endif
    [self.pageView setMidiEventText:
        @"0 MIDI EVENTS  •  SEND 0  DROP 0  LATE 0  CLK 0"];

    self.songWindow.changeHandler = ^(NSString* summary) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        const char* text = summary.UTF8String;
        [owner.workspace appendConsoleMessage:text
                ? std::string("Song: ") + text : "Song updated"
            error:NO];
        [owner commitSongProjectEdit:YES];
    };
    self.songWindow.modeChangeHandler = ^(BOOL enabled) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_state->songPlaybackEnabled = enabled;
        // Mode is an escape/control switch, not an arrangement edit. Apply it
        // immediately even while REAPER runs so Song mode can always be
        // disabled after the final non-looping row.
        owner->_deferredSongRuntimePublication = false;
        [owner commitProject:YES];
    };
    self.songWindow.loopChangeHandler = ^(BOOL enabled) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        const bool transportRunning = reaperTransportIsPlaying(
            *owner->_plugin).value_or(owner->_state->playing);
        if (transportRunning) {
            // LOOP SONG is a live transport decision. Persist it without
            // replacing the active runtime, then send the new value through
            // the audio-thread mailbox so the current row keeps its phase.
            [owner commitProjectWithoutRuntime:YES];
            if (owner->_playingSongArrangementValid)
                owner->_playingSongArrangement.loop = enabled;
        } else {
            owner->_deferredSongRuntimePublication = false;
            [owner commitProject:YES];
        }
        owner->_plugin->songLoopEnabled.store(enabled,
            std::memory_order_relaxed);
        owner->_plugin->songLoopRevision.fetch_add(1u,
            std::memory_order_release);
        if (owner->_plugin->host && owner->_plugin->host->request_process)
            owner->_plugin->host->request_process(owner->_plugin->host);
        owner->_state->status = enabled
            ? "Song loop enabled" : "Song loop disabled";
        [owner.workspace appendConsoleMessage:owner->_state->status error:NO];
        [owner.workspace refreshPlaybackDisplay];
    };
    self.songWindow.launchHandler = ^(NSUInteger row, NSInteger quantization) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner) return;
        owner->_plugin->songLaunchRow.store(static_cast<uint32_t>(row),
            std::memory_order_relaxed);
        const auto launch = static_cast<SongLaunchQuantization>(
            std::clamp<NSInteger>(quantization, 0, 3));
        owner->_plugin->songLaunchQuantization.store(
            static_cast<uint32_t>(launch), std::memory_order_relaxed);
        [owner cancelRuntimePublication];
        owner->_plugin->songArrangementUpdatePending.store(false,
            std::memory_order_release);
        ProjectDocument document = [owner currentDocument];
        if (!queueSongDocument(*owner->_plugin, document,
                static_cast<std::size_t>(row), launch)) {
            owner->_state->status =
                "Could not prepare the selected Song row for queueing";
            [owner.workspace appendConsoleMessage:owner->_state->status
                error:YES];
            [owner.workspace reloadModel];
            return;
        }
        owner->_deferredSongRuntimePublication = false;
        owner->_playingSongArrangement = document.song;
        owner->_playingSongArrangementValid = true;
        [owner.songWindow setPendingPlaybackRow:row valid:YES
            quantization:static_cast<NSInteger>(launch)];
        owner->_state->status = "Queued quantized Song row "
            + std::to_string(row + 1u);
        [owner.workspace appendConsoleMessage:owner->_state->status error:NO];
    };
    self.songWindow.saveProjectHandler = ^{
        [weakSelf presentSaveSongProject];
    };
    self.songWindow.loadProjectHandler = ^{
        [weakSelf presentLoadSongProject];
    };
    [self configureHostDevices];
    return self;
}

- (void)dealloc
{
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    // Host callbacks must disappear before the focused frames/pages are closed.
    _reaperTextInput.reset();
#endif
    const auto previous = static_cast<MidiStepRecordMode>(
        _plugin->midiStepRecordMode.exchange(
            static_cast<uint8_t>(MidiStepRecordMode::Off),
            std::memory_order_acq_rel));
    if (previous != MidiStepRecordMode::Off) {
        _plugin->requestMidiMonitorRelease.store(
            true, std::memory_order_release);
    }
    [self stopTimer];
    [self.pageView detachFromPlugin];
    [self.songWindow close];
    [self.helpWindow close];
}

- (void)consumeMidiStepCaptures
{
    if (!_state) return;
    MidiStepCapture capture;
    bool changed = false;
    bool reportedSongConflict = false;
    while (_plugin->midiStepCaptures.pop(capture)) {
        const bool live = capture.mode == MidiStepRecordMode::LiveQuantized
            || capture.mode == MidiStepRecordMode::LiveUnquantized;
        if (live && _state->songPlaybackEnabled) {
            if (!reportedSongConflict) {
                reportedSongConflict = true;
                [self.workspace appendConsoleMessage:
                    "LIVE recording targets one selected pattern; turn SONG TRANSPORT off first"
                    error:YES];
            }
            continue;
        }
        const auto result = s3g::tracker::recordMidiStep(_state->session,
            capture.mode, capture, _plugin->sampleRate,
            &_midiLiveRecordState, _state->trackerRowJump);
        if (result.recorded()) {
            changed = true;
            std::ostringstream message;
            const char* label = result.release
                ? (capture.mode == MidiStepRecordMode::LiveQuantized
                        ? "LIVE Q REL" : "LIVE MT REL")
                : capture.mode == MidiStepRecordMode::Step
                ? "STEP REC" : capture.mode
                    == MidiStepRecordMode::LiveQuantized
                ? "LIVE Q REC" : "LIVE MT REC";
            message << label << " CH" << static_cast<unsigned>(capture.channel)
                    << " note " << static_cast<unsigned>(capture.note)
                    << " → lane " << (result.track + 1u)
                    << ", row " << (result.row + 1u);
            if (result.holdRows > 0u)
                message << ", " << result.holdRows << " HLD";
            if (capture.mode == MidiStepRecordMode::LiveUnquantized
                && result.fxPair < s3g::tracker::kFxPairCount) {
                message << ", MT " << std::lround(
                    static_cast<double>(result.microTime) * 100.0) << '%';
                if (result.timingClamped) message << " (clamped)";
            }
            [self.workspace appendConsoleMessage:message.str() error:NO];
        } else if (result.code == MidiStepRecordCode::FxUnavailable) {
            [self.workspace appendConsoleMessage:
                "LIVE MT needs an empty SEQ1 or SEQ2 cell on the captured row"
                error:YES];
        } else if (result.code == MidiStepRecordCode::TimingUnavailable) {
            [self.workspace appendConsoleMessage:capture.rowKnown
                    ? "LIVE MT requires a nonzero Micro Time range"
                    : "LIVE recording requires running REAPER transport and a known tracker row"
                error:YES];
        }
    }
    const uint64_t dropped = _plugin->midiStepCaptures.droppedCount();
    if (dropped != _reportedStepRecordDrops) {
        _reportedStepRecordDrops = dropped;
        [self.workspace appendConsoleMessage:
            "MIDI record input overflowed; reduce controller density"
            error:YES];
    }
    if (changed) [self commitProject:YES];
}

- (void)disarmMidiStepRecording
{
    if (!_state) return;
    _midiLiveRecordState.clear();
    _state->midiStepRecordMode = MidiStepRecordMode::Off;
    const auto previous = static_cast<MidiStepRecordMode>(
        _plugin->midiStepRecordMode.exchange(
            static_cast<uint8_t>(MidiStepRecordMode::Off),
            std::memory_order_acq_rel));
    if (previous != MidiStepRecordMode::Off) {
        _plugin->requestMidiMonitorRelease.store(
            true, std::memory_order_release);
        if (_plugin->host && _plugin->host->request_process)
            _plugin->host->request_process(_plugin->host);
    }
    [self.workspace reloadModel];
}

- (void)updateMidiMonitorChannel
{
    if (!_state || _state->session.pattern.tracks.empty()) {
        if (_state) _state->midiRecordTrack = 0u;
        _plugin->midiRecordTrack.store(0u, std::memory_order_release);
        _plugin->midiMonitorChannel.store(0u, std::memory_order_release);
        return;
    }
    const auto lane = std::min(_state->midiRecordTrack,
        _state->session.pattern.tracks.size() - 1u);
    _state->midiRecordTrack = lane;
    _plugin->midiRecordTrack.store(
        static_cast<uint32_t>(lane), std::memory_order_release);
    const uint8_t channel = static_cast<uint8_t>(std::clamp<int>(
        _state->session.pattern.tracks[lane].midiChannel, 1, 16) - 1);
    _plugin->midiMonitorChannel.store(channel, std::memory_order_release);
}

- (void)configureHostDevices
{
    if (!_state || !self.workspace) return;
    _state->midiRoute = "1 OUT • 1 REC IN • CH 1–16";
    _state->audioOutputDevice = "REAPER HOST AUDIO";
    _state->audioAvailable = false;
    [self.workspace reloadModel];
}

- (void)refreshSongPatterns
{
    NSMutableArray<NSString*>* ids = [[NSMutableArray alloc] init];
    NSMutableArray<NSString*>* names = [[NSMutableArray alloc] init];
    NSMutableArray<NSNumber*>* patternLengths = [[NSMutableArray alloc] init];
    NSMutableArray<NSNumber*>* patternLaneCounts =
        [[NSMutableArray alloc] init];
    for (const auto& entry : _state->patternBank.entries) {
        [ids addObject:[NSString stringWithUTF8String:entry.id.c_str()]];
        [names addObject:[NSString stringWithUTF8String:
            entry.pattern.name.c_str()]];
        [patternLengths addObject:@(longestPatternColumnLength(
            entry.pattern))];
        [patternLaneCounts addObject:@(entry.pattern.tracks.size())];
    }
    NSString* active = [NSString stringWithUTF8String:
        _state->patternBank.activePatternId.c_str()];
    [self.songWindow setAvailablePatternIds:ids patternNames:names
        patternLengths:patternLengths patternLaneCounts:patternLaneCounts
        activePatternId:active];
}

- (void)refreshSongWarps
{
    if (!_state || !self.songWindow) return;
    [self.songWindow setTimingWarpLibrary:_state->session.warpLibrary];
}

- (void)presentSaveSongProject
{
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"Save Tracker Song + Patterns";
    panel.prompt = @"Save";
    panel.canCreateDirectories = YES;
    panel.nameFieldStringValue = @"Tracker Song.s3gt";
    if (UTType* type = [UTType typeWithFilenameExtension:@"s3gt"])
        panel.allowedContentTypes = @[ type ];
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || response != NSModalResponseOK || !panel.URL) return;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path) return;
        const auto result = s3g::tracker::saveProjectDocumentAtomically(
            [owner currentDocument], path);
        if (!result.ok()) {
            const std::string message = "Could not save Song + Patterns: "
                + result.message;
            owner->_state->status = message;
            [owner.workspace appendConsoleMessage:message error:YES];
        } else {
            owner->_state->status = "Saved Song + Patterns";
            [owner.workspace appendConsoleMessage:
                std::string("Saved Song + Patterns to ") + path error:NO];
        }
        [owner.workspace reloadModel];
    };
    if (self.pageView.window)
        [panel beginSheetModalForWindow:self.pageView.window
            completionHandler:completion];
    else
        [panel beginWithCompletionHandler:completion];
}

- (void)presentLoadSongProject
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Load Tracker Song + Patterns";
    panel.prompt = @"Load";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if (UTType* type = [UTType typeWithFilenameExtension:@"s3gt"])
        panel.allowedContentTypes = @[ type ];
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || response != NSModalResponseOK || !panel.URL) return;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path) return;
        ProjectDocument document;
        const auto result = s3g::tracker::loadProjectDocument(path, document);
        if (!result.ok()) {
            const std::string message = "Could not load Song + Patterns: "
                + result.message;
            owner->_state->status = message;
            [owner.workspace appendConsoleMessage:message error:YES];
            [owner.workspace reloadModel];
            return;
        }
        [owner applyDocument:document];
        [owner commitProject:YES];
        owner->_state->status = "Loaded Song + Patterns";
        [owner.workspace appendConsoleMessage:
            std::string("Loaded Song + Patterns from ") + path error:NO];
        [owner.workspace reloadModel];
    };
    if (self.pageView.window)
        [panel beginSheetModalForWindow:self.pageView.window
            completionHandler:completion];
    else
        [panel beginWithCompletionHandler:completion];
}

- (void)presentExportAssetPack:(const TrackerAssetPack&)pack
{
    std::string encoded;
    const auto encodedResult = s3g::tracker::encodeTrackerAssetPack(
        pack, encoded);
    if (!encodedResult.ok()) {
        const std::string message = "Could not encode asset pack: "
            + encodedResult.message;
        _state->status = message;
        [self.workspace appendConsoleMessage:message error:YES];
        [self.workspace reloadModel];
        return;
    }
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.title = @"Export Tracker Phrase + Burst Pack";
    panel.prompt = @"Export";
    panel.canCreateDirectories = YES;
    NSString* base = pack.name.empty() ? @"Tracker Assets"
        : [NSString stringWithUTF8String:pack.name.c_str()];
    panel.nameFieldStringValue = [base stringByAppendingPathExtension:@"s3gpack"];
    if (UTType* type = [UTType typeWithFilenameExtension:@"s3gpack"])
        panel.allowedContentTypes = @[ type ];
    // Snapshot at dialog creation, just as the former NSData capture did.
    const TrackerAssetPack exportPack = pack;
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || response != NSModalResponseOK || !panel.URL) return;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path) return;
        const auto result = s3g::tracker::saveTrackerAssetPackAtomically(exportPack, path);
        if (!result.ok()) {
            const std::string message = "Could not export asset pack: "
                + result.message;
            owner->_state->status = message;
            [owner.workspace appendConsoleMessage:message error:YES];
        } else {
            owner->_state->status = "Exported Tracker asset pack";
            [owner.workspace appendConsoleMessage:
                std::string("Exported asset pack to ")
                    + panel.URL.fileSystemRepresentation error:NO];
        }
        [owner.workspace reloadModel];
    };
    if (self.pageView.window)
        [panel beginSheetModalForWindow:self.pageView.window
            completionHandler:completion];
    else
        [panel beginWithCompletionHandler:completion];
}

- (void)presentImportAssetPack
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Import Tracker Phrase + Burst Pack";
    panel.prompt = @"Import";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if (UTType* type = [UTType typeWithFilenameExtension:@"s3gpack"])
        panel.allowedContentTypes = @[ type ];
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
        S3GTrackerClapCoordinator* owner = weakSelf;
        if (!owner || response != NSModalResponseOK || !panel.URL) return;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path) return;
        TrackerAssetPack pack;
        auto result = s3g::tracker::loadTrackerAssetPack(path, pack);
        ProjectDocument document = [owner currentDocument];
        s3g::tracker::AssetPackImportReport report;
        if (result.ok()) result = s3g::tracker::importTrackerAssetPack(
            pack, document, &report);
        if (!result.ok()) {
            const std::string message = "Could not import asset pack"
                + (result.location.empty() ? std::string {}
                    : " at " + result.location)
                + ": " + result.message;
            owner->_state->status = message;
            [owner.workspace appendConsoleMessage:message error:YES];
            [owner.workspace reloadModel];
            return;
        }
        [owner applyDocument:document];
        [owner commitProject:YES];
        std::ostringstream summary;
        summary << "Imported asset pack: " << report.burstsAdded
                << " Bursts + " << report.phrasesAdded << " Phrases";
        if (report.burstsReused > 0u || report.phrasesReused > 0u)
            summary << " (reused " << report.burstsReused << " Bursts, "
                    << report.phrasesReused << " Phrases)";
        owner->_state->status = summary.str();
        [owner.workspace appendConsoleMessage:summary.str() error:NO];
        [owner.workspace reloadModel];
    };
    if (self.pageView.window)
        [panel beginSheetModalForWindow:self.pageView.window
            completionHandler:completion];
    else
        [panel beginWithCompletionHandler:completion];
}

- (ProjectDocument)currentDocument
{
    return _documentController->snapshot([self.songWindow songArrangement]);
}

- (void)applyDocument:(const ProjectDocument&)document
{
    _midiLiveRecordState.clear();
    const auto midiDocument = _documentController->apply(document);
    _state->status = "REAPER host sync • MIDI output ready";
    [self refreshSongWarps];
    // Song rows validate their mute masks against the pattern catalog. Load
    // that catalog first so rows referencing anything other than the initial
    // A01 placeholder do not have their saved lane mutes pruned as unknown.
    [self refreshSongPatterns];
    [self.songWindow setSongArrangement:midiDocument.song];
    self.songWindow.playbackEnabled = _state->songPlaybackEnabled;
    [self updateMidiMonitorChannel];
    [self.workspace reloadModel];
}

- (void)updateHistoryAvailability
{
    if (!_state) return;
    _documentController->updateHistoryAvailability();
}

- (void)resetHistory:(const ProjectDocument&)document
{
    const auto result = _documentController->resetHistory(document);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:
            "Could not initialize Tracker edit history: " + result.message
            error:YES];
    }
    [self updateHistoryAvailability];
    [self.workspace reloadModel];
}

- (void)recordHistory:(const ProjectDocument&)document
{
    const auto result = _documentController->recordHistory(document);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:
            "Could not record Tracker edit history: " + result.message
            error:YES];
    }
    [self updateHistoryAvailability];
}

- (void)undoProject
{
    ProjectDocument document;
    const auto result = _documentController->undo(document);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:result.message error:YES];
        [self updateHistoryAvailability];
        [self.workspace reloadModel];
        return;
    }
    [self cancelRuntimePublication];
    [self applyDocument:document];
    publishDocument(*_plugin, std::move(document), true);
    [self updateHistoryAvailability];
    _state->status = "Undid Tracker edit";
    [self.workspace appendConsoleMessage:_state->status error:NO];
    [self.workspace reloadModel];
}

- (void)redoProject
{
    ProjectDocument document;
    const auto result = _documentController->redo(document);
    if (!result.ok()) {
        [self.workspace appendConsoleMessage:result.message error:YES];
        [self updateHistoryAvailability];
        [self.workspace reloadModel];
        return;
    }
    [self cancelRuntimePublication];
    [self applyDocument:document];
    publishDocument(*_plugin, std::move(document), true);
    [self updateHistoryAvailability];
    _state->status = "Redid Tracker edit";
    [self.workspace appendConsoleMessage:_state->status error:NO];
    [self.workspace reloadModel];
}

- (void)commitProject:(BOOL)dirty
{
    if (!_state) return;
    ProjectDocument document = [self currentDocument];
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    [self updateMidiMonitorChannel];
    [self refreshSongPatterns];
    if (dirty) [self recordHistory:document];
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    [self scheduleRuntimePublication];
    [self.workspace reloadModel];
}

- (void)commitProjectWithoutRuntime:(BOOL)dirty
{
    if (!_state) return;
    ProjectDocument document = [self currentDocument];
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    [self updateMidiMonitorChannel];
    [self refreshSongPatterns];
    if (dirty) [self recordHistory:document];
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    [self.workspace reloadModel];
}

- (void)commitSongProjectEdit:(BOOL)dirty
{
    if (!_state) return;
    const bool transportRunning = reaperTransportIsPlaying(*_plugin)
        .value_or(_state->playing);
    if (!transportRunning) {
        _deferredSongRuntimePublication = false;
        [self commitProject:dirty];
        return;
    }

    // Hand an edited arrangement over at the next Song-row boundary. A fresh
    // runtime starts on the row that naturally follows the one currently
    // sounding, so future-row edits are heard without restarting the Song or
    // mutating scheduler-owned vectors on the audio thread.
    [self cancelRuntimePublication];
    const int32_t priorPendingRow = _plugin->visualPendingSongRow.load(
        std::memory_order_acquire);
    const auto priorQuantization = static_cast<SongLaunchQuantization>(
        std::min<uint32_t>(_plugin->songLaunchQuantization.load(
            std::memory_order_acquire), static_cast<uint32_t>(
                SongLaunchQuantization::NextSongRow)));
    cancelQueuedVariation(*_plugin);
    ProjectDocument document = [self currentDocument];
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    [self updateMidiMonitorChannel];
    [self refreshSongPatterns];
    if (dirty) [self recordHistory:document];

    const int32_t audibleRow = _plugin->visualSongRow.load(
        std::memory_order_acquire);
    const std::size_t currentRow = audibleRow >= 0
        ? static_cast<std::size_t>(audibleRow)
        : _state->songPlaybackRow;
    // Row drag can change numeric indices while the old immutable runtime is
    // still sounding. Resolve that row through its stable ID before choosing
    // the successor in the edited arrangement.
    std::size_t editedCurrentRow = currentRow;
    if (_playingSongArrangementValid
        && currentRow < _playingSongArrangement.rows.size()) {
        const uint32_t currentId = _playingSongArrangement.rows[currentRow].id;
        if (currentId != 0u) {
            const auto found = std::find_if(document.song.rows.begin(),
                document.song.rows.end(), [currentId](
                    const s3g::tracker::SongRow& row) {
                    return row.id == currentId;
                });
            if (found != document.song.rows.end())
                editedCurrentRow = static_cast<std::size_t>(
                    found - document.song.rows.begin());
        }
    }
    const bool hasNextRow = editedCurrentRow + 1u < document.song.rows.size();
    const bool wraps = !document.song.rows.empty() && document.song.loop;
    std::optional<std::size_t> preservedPendingRow;
    if (priorPendingRow >= 0 && _playingSongArrangementValid
        && static_cast<std::size_t>(priorPendingRow)
            < _playingSongArrangement.rows.size()) {
        const uint32_t pendingId = _playingSongArrangement.rows[
            static_cast<std::size_t>(priorPendingRow)].id;
        const auto found = std::find_if(document.song.rows.begin(),
            document.song.rows.end(), [pendingId](
                const s3g::tracker::SongRow& row) {
                return pendingId != 0u && row.id == pendingId;
            });
        if (found != document.song.rows.end())
            preservedPendingRow = static_cast<std::size_t>(
                found - document.song.rows.begin());
    }
    if (preservedPendingRow || hasNextRow || wraps) {
        const std::size_t launchRow = preservedPendingRow
            ? *preservedPendingRow : hasNextRow ? editedCurrentRow + 1u : 0u;
        const auto quantization = preservedPendingRow
            ? priorQuantization : SongLaunchQuantization::NextSongRow;
        _plugin->songLaunchRow.store(static_cast<uint32_t>(launchRow),
            std::memory_order_relaxed);
        _plugin->songLaunchQuantization.store(static_cast<uint32_t>(
            quantization),
            std::memory_order_relaxed);
        const SongArrangement updatedArrangement = document.song;
        _plugin->songArrangementUpdatePending.store(true,
            std::memory_order_release);
        if (queueSongDocument(*_plugin, std::move(document), launchRow,
                quantization, dirty)) {
            _playingSongArrangement = updatedArrangement;
            _playingSongArrangementValid = true;
            _deferredSongRuntimePublication = false;
            _state->status = preservedPendingRow
                ? "Song update preserved queued row "
                    + std::to_string(launchRow + 1u)
                : "Song update scheduled for row "
                    + std::to_string(launchRow + 1u);
            [self.workspace appendConsoleMessage:_state->status error:NO];
            [self.workspace reloadModel];
            return;
        }
        _plugin->songArrangementUpdatePending.store(false,
            std::memory_order_release);
        document = [self currentDocument];
    }

    // A non-looping final row has no future boundary to hand over to. Keep
    // the edit persistent and prepare it once REAPER stops.
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    _deferredSongRuntimePublication = true;
    _state->status = "Song edit saved • no future row before REAPER stop";
    [self.workspace reloadModel];
}

- (void)scheduleRuntimePublication
{
    _runtimePublicationPending = true;
    [self.runtimePublicationTimer invalidate];
    __weak S3GTrackerClapCoordinator* weakSelf = self;
    self.runtimePublicationTimer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
        repeats:NO block:^(NSTimer*) {
            [weakSelf flushRuntimePublication];
        }];
    [[NSRunLoop mainRunLoop] addTimer:self.runtimePublicationTimer
        forMode:NSRunLoopCommonModes];
}

- (void)cancelRuntimePublication
{
    [self.runtimePublicationTimer invalidate];
    self.runtimePublicationTimer = nil;
    _runtimePublicationPending = false;
}

- (void)flushRuntimePublication
{
    [self.runtimePublicationTimer invalidate];
    self.runtimePublicationTimer = nil;
    if (!_runtimePublicationPending) return;
    _runtimePublicationPending = false;
    if (!publishStoredDocumentRuntime(*_plugin)) {
        [self.workspace appendConsoleMessage:
            "Could not prepare the edited Tracker playback runtime"
            error:YES];
    }
}

- (BOOL)installPatternVariation:
    (const s3g::tracker::PatternVariationRequest&)variation
{
    [self cancelRuntimePublication];
    if (_state->patternBank.entries.size()
            >= s3g::tracker::kMaximumPatternBankEntries
        || !syncSessionToActivePattern(*_state)) return NO;
    const std::string sourceId = _state->patternBank.activePatternId;
    const auto* source = _state->patternBank.findEntry(sourceId);
    const std::string id = nextPatternId(_state->patternBank);
    if (!source || id.empty()) return NO;
    _state->patternBank.entries.push_back(variationPatternEntry(
        *source, id, variation));

    if (variation.launch == PatternVariationLaunch::None) {
        _state->status = "Created variation " + id
            + "; active pattern remains " + sourceId;
        [self refreshSongPatterns];
        [self commitProjectWithoutRuntime:YES];
        [self.workspace appendConsoleMessage:_state->status error:NO];
        return YES;
    }

    _state->patternBank.activePatternId = id;
    if (!loadActivePatternIntoSession(*_state)) return NO;
    _state->session.selectedRow = 0u;
    ProjectDocument document = [self currentDocument];
    _state->patternBank = document.patternBank;
    (void)loadActivePatternIntoSession(*_state);
    const ProjectDocument historyDocument = document;
    const bool playing = _plugin->visualPlaying.load(
        std::memory_order_acquire);
    bool installed = true;
    if (playing) {
        installed = queueVariationDocument(*_plugin, std::move(document),
            variation.launch, true);
    } else {
        publishDocument(*_plugin, std::move(document), true);
    }
    if (!installed) {
        _state->patternBank.entries.pop_back();
        _state->patternBank.activePatternId = sourceId;
        (void)loadActivePatternIntoSession(*_state);
        _state->status = "Could not prepare variation runtime";
        [self refreshSongPatterns];
        [self.workspace reloadModel];
        return NO;
    }
    [self recordHistory:historyDocument];
    _state->status = playing
        ? "Queued variation " + id + " for quantized launch"
        : "Selected variation " + id;
    [self refreshSongPatterns];
    [self.workspace reloadModel];
    [self.workspace appendConsoleMessage:_state->status error:NO];
    return YES;
}

- (void)selectPattern:(const std::string&)patternId
{
    if (patternId == _state->patternBank.activePatternId
        || !_state->patternBank.findEntry(patternId)) return;
    if (!syncSessionToActivePattern(*_state)
        || !_state->patternBank.selectPattern(patternId)
        || !loadActivePatternIntoSession(*_state)) return;
    _state->status = "Selected pattern " + patternId;
    [self refreshSongPatterns];
    [self commitProject:YES];
}

- (void)addPattern:(bool)duplicate
{
    if (_state->patternBank.entries.size()
        >= s3g::tracker::kMaximumPatternBankEntries) return;
    if (!syncSessionToActivePattern(*_state)) return;
    const auto* source = _state->patternBank.findEntry(
        _state->patternBank.activePatternId);
    const std::string id = nextPatternId(_state->patternBank);
    if (!source || id.empty()) return;
    _state->patternBank.entries.push_back(newPatternEntry(
        *source, id, duplicate));
    _state->patternBank.activePatternId = id;
    (void)loadActivePatternIntoSession(*_state);
    _state->session.selectedRow = 0u;
    _state->status = duplicate ? "Duplicated pattern " + id
                               : "Created pattern " + id;
    [self refreshSongPatterns];
    [self commitProject:YES];
}

- (void)renamePattern
{
    auto* entry = _state->patternBank.findEntry(
        _state->patternBank.activePatternId);
    if (!entry) return;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Rename Pattern";
    [alert addButtonWithTitle:@"Rename"];
    [alert addButtonWithTitle:@"Cancel"];
    NSTextField* field = [[NSTextField alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, 320.0, 24.0)];
    S3GTrackerStyleTextField(field, NSTextAlignmentLeft);
    field.stringValue = [NSString stringWithUTF8String:entry->pattern.name.c_str()];
    alert.accessoryView = field;
    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    NSString* value = [field.stringValue stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (value.length == 0u) return;
    const char* text = value.UTF8String;
    entry->pattern.name = text ? text : entry->pattern.name;
    _state->session.pattern.name = entry->pattern.name;
    [self refreshSongPatterns];
    [self commitProjectWithoutRuntime:YES];
}

- (void)deletePattern
{
    if (_state->patternBank.entries.size() <= 1u) return;
    const std::string id = _state->patternBank.activePatternId;
    const auto arrangement = [self.songWindow songArrangement];
    for (const auto& row : arrangement.rows) {
        if (row.patternId == id) {
            _state->status = "Cannot delete a pattern used by Song mode";
            [self.workspace appendConsoleMessage:_state->status error:YES];
            return;
        }
    }
    const auto found = std::find_if(_state->patternBank.entries.begin(),
        _state->patternBank.entries.end(), [&](const auto& entry) {
            return entry.id == id;
        });
    if (found == _state->patternBank.entries.end()) return;
    const auto index = static_cast<std::size_t>(std::distance(
        _state->patternBank.entries.begin(), found));
    _state->patternBank.entries.erase(found);
    _state->patternBank.activePatternId = _state->patternBank.entries[
        std::min(index, _state->patternBank.entries.size() - 1u)].id;
    (void)loadActivePatternIntoSession(*_state);
    [self refreshSongPatterns];
    [self commitProject:YES];
}

- (void)executeCommand:(const std::string&)command
{
    s3g::tracker::ClapCommandServices services;
    services.message = [self](const std::string& message, bool error) {
        [self.workspace appendConsoleMessage:message error:error];
    };
    services.showHelp = [self] { [self.pageView showPage:S3GTrackerClapPageHelp]; };
    services.undo = [self] { [self undoProject]; };
    services.redo = [self] { [self redoProject]; };
    services.commitRuntime = [self] { [self commitProject:YES]; };
    services.commitDocument = [self] { [self commitProjectWithoutRuntime:YES]; };
    services.refreshWarps = [self] { [self refreshSongWarps]; };
    services.refreshUI = [self] { [self.workspace reloadModel]; };
    services.updateMonitor = [self] { [self updateMidiMonitorChannel]; };
    services.installVariation = [self](const s3g::tracker::PatternVariationRequest& variation) {
        return bool([self installPatternVariation:variation]);
    };
    services.requestHostContinue = [self] { return requestHostContinue(*_plugin); };
    services.requestHostStop = [self] { return requestHostStop(*_plugin); };
    services.panic = [self] { _plugin->requestPanic.store(true, std::memory_order_release); };
    s3g::tracker::executeClapCommand(*_state, command, services);
}

- (void)pollDisplay:(NSTimer*)timer
{
    (void)timer;
    drainRetiredRuntimes(*_plugin);
    [self consumeMidiStepCaptures];
    const bool playing = reaperTransportIsPlaying(*_plugin).value_or(
        _plugin->visualPlaying.load(std::memory_order_relaxed));
    if (!playing)
        _plugin->visualPlaying.store(false, std::memory_order_relaxed);
    if (playing && !_transportWasPlaying) {
        _playingSongArrangement = [self.songWindow songArrangement];
        _playingSongArrangementValid = true;
    } else if (!playing && _transportWasPlaying) {
        _playingSongArrangementValid = false;
        if (_deferredSongRuntimePublication) {
            _deferredSongRuntimePublication = false;
            if (publishStoredDocumentRuntime(*_plugin)) {
                [self.workspace appendConsoleMessage:
                    "Song edits applied for the next transport start"
                    error:NO];
            } else {
                [self.workspace appendConsoleMessage:
                    "Could not prepare the edited Song playback runtime"
                    error:YES];
            }
        }
    }
    _transportWasPlaying = playing;
    _state->playing = playing;
    const double callbackTempo = _plugin->visualHostTempo.load(
        std::memory_order_relaxed);
    // REAPER can suspend audio processing while stopped, so no new CLAP
    // transport snapshot arrives after its master tempo field changes. Query
    // that host value on the GUI/main thread only in the stopped state. While
    // playing, keep the sample-accurate process transport authoritative so a
    // tempo map is represented at the actual playback position.
    _state->hostBpm = playing ? callbackTempo
        : reaperHostTempo(*_plugin).value_or(callbackTempo);
    [self.pageView setHostBpm:_state->hostBpm];
    _state->paused = false;
    VisualPlaybackFrame captured;
    for (std::size_t track = 0u;
         track < s3g::tracker::kMaximumTrackCount; ++track) {
        captured.notePlayheads[track] = _plugin->notePlayheads[track].load(
            std::memory_order_relaxed);
        VisualNoteHitEvent hit;
        const bool newHit = _plugin->visualNoteHits[track].readLatest(
            _consumedNoteHitSequences[track], hit);
        captured.noteHits[track] = playing && newHit;
        if (newHit) {
            captured.noteHitRows[track] = hit.row;
            captured.noteHitSampleTimes[track] = hit.absoluteSampleTime;
        }
        captured.instrumentPlayheads[track]
            = _plugin->instrumentPlayheads[track].load(
                std::memory_order_relaxed);
        captured.velocityPlayheads[track]
            = _plugin->velocityPlayheads[track].load(
                std::memory_order_relaxed);
        for (std::size_t pair = 0u; pair < s3g::tracker::kFxPairCount; ++pair) {
            captured.fxActionPlayheads[track][pair]
                = _plugin->fxActionPlayheads[track][pair].load(
                    std::memory_order_relaxed);
            captured.fxValuePlayheads[track][pair]
                = _plugin->fxValuePlayheads[track][pair].load(
                    std::memory_order_relaxed);
        }
    }
    captured.songRow = _plugin->visualSongRow.load(
        std::memory_order_relaxed);
    captured.pendingSongRow = _plugin->visualPendingSongRow.load(
        std::memory_order_relaxed);
    captured.pendingSongQuantization
        = _plugin->visualPendingSongQuantization.load(
            std::memory_order_relaxed);
    captured.subrowPhase = _plugin->visualSubrowPhase.load(
        std::memory_order_relaxed);
    captured.timingWarpTick = _plugin->visualTimingWarpTick.load(
        std::memory_order_relaxed);
    if (playing && _visualFramePrimed) {
        _state->notePlayheads = _pendingVisualFrame.notePlayheads;
        _state->noteHits = _pendingVisualFrame.noteHits;
        _state->noteHitRows = _pendingVisualFrame.noteHitRows;
        _state->noteHitSampleTimes
            = _pendingVisualFrame.noteHitSampleTimes;
        _state->instrumentPlayheads
            = _pendingVisualFrame.instrumentPlayheads;
        _state->velocityPlayheads = _pendingVisualFrame.velocityPlayheads;
        _state->fxActionPlayheads = _pendingVisualFrame.fxActionPlayheads;
        _state->fxValuePlayheads = _pendingVisualFrame.fxValuePlayheads;
        _state->subrowPlaybackPhase = _pendingVisualFrame.subrowPhase;
        _state->timingWarpPlaybackTick
            = _pendingVisualFrame.timingWarpTick;
    } else {
        _state->noteHits.fill(false);
        if (!playing) {
            _state->subrowPlaybackPhase = 0.0f;
            _state->timingWarpPlaybackTick = 0u;
        }
    }
    const int32_t songRow = playing && _visualFramePrimed
        ? _pendingVisualFrame.songRow : captured.songRow;
    const int32_t pendingSongRow = playing && _visualFramePrimed
        ? _pendingVisualFrame.pendingSongRow : captured.pendingSongRow;
    const uint32_t pendingSongQuantization = playing && _visualFramePrimed
        ? _pendingVisualFrame.pendingSongQuantization
        : captured.pendingSongQuantization;
    _pendingVisualFrame = std::move(captured);
    _visualFramePrimed = playing;
    _state->songPlaybackActive = _state->playing
        && _state->songPlaybackEnabled && songRow >= 0;
    _state->songPlaybackRowValid = songRow >= 0;
    _state->songPlaybackRow = songRow >= 0
        ? static_cast<std::size_t>(songRow) : 0u;
    _state->songPlaybackPatternId.clear();
    _state->songPlaybackMutedTracks = 0u;
    _state->timingWarpPlaybackActive = false;
    _state->timingWarpPlaybackFromSong = false;
    _state->timingWarpPlaybackCycleTicks = 1u;
    _state->timingWarpPlaybackStack.clear();
    if (_state->playing && !_state->songPlaybackEnabled
        && _state->session.transport.timingWarpEnabled) {
        _state->timingWarpPlaybackActive = true;
        _state->timingWarpPlaybackCycleTicks = std::max<uint32_t>(1u,
            _state->session.transport.warpCycleTicks);
        _state->timingWarpPlaybackStack
            = _state->session.transport.timingWarp;
    }
    if (_state->songPlaybackActive) {
        const auto arrangement = _playingSongArrangementValid
            ? _playingSongArrangement : [self.songWindow songArrangement];
        if (_state->songPlaybackRow < arrangement.rows.size()) {
            const auto& activeRow = arrangement.rows[
                _state->songPlaybackRow];
            _state->songPlaybackPatternId = activeRow.patternId;
            _state->songPlaybackMutedTracks = activeRow.mutedTracks;
            if (activeRow.timingWarpLibraryIndex) {
                const auto* entry = _state->session.warpLibrary.entry(
                    *activeRow.timingWarpLibraryIndex);
                if (entry) {
                    _state->timingWarpPlaybackActive = true;
                    _state->timingWarpPlaybackFromSong = true;
                    _state->timingWarpPlaybackCycleTicks
                        = std::max<uint32_t>(1u, entry->cycleTicks);
                    _state->timingWarpPlaybackStack = entry->stack;
                }
            }
        }
    }
    [self.songWindow setPlaybackRow:_state->songPlaybackRow
        valid:_state->songPlaybackRowValid];
    [self.songWindow setPendingPlaybackRow:pendingSongRow >= 0
            ? static_cast<NSUInteger>(pendingSongRow) : 0u
        valid:pendingSongRow >= 0
        quantization:static_cast<NSInteger>(std::min<uint32_t>(
            pendingSongQuantization, 3u))];
    // Queueing follows the REAPER clock, not the presence of an active Song
    // row. This keeps it available for looping/non-looping playback and after
    // a non-looping arrangement reaches its end while REAPER keeps running.
    [self.songWindow setPlaybackLocked:_state->playing];
    _state->sentEventCount = _plugin->sentEvents.load(
        std::memory_order_relaxed);
    _state->droppedEventCount = _plugin->droppedEvents.load(
        std::memory_order_relaxed);
    _state->status = _state->playing
        ? "REAPER PLAYING • sample-accurate CLAP MIDI"
        : "REAPER STOPPED • MIDI output ready";
    _state->lastEvent = std::to_string(_state->sentEventCount)
        + " MIDI EVENTS";
    [self.pageView setMidiEventText:[NSString stringWithFormat:
        @"%@  •  SEND %llu  DROP %llu  LATE %llu  CLK %llu",
        [NSString stringWithUTF8String:_state->lastEvent.c_str()],
        _state->sentEventCount, _state->droppedEventCount,
        _state->audioLateEventCount, _state->audioClockFaultCount]];
    [self.workspace refreshPlaybackDisplay];
}

- (void)startTimer
{
    if (self.displayTimer) return;
    self.displayTimer = [NSTimer timerWithTimeInterval:(1.0 / 60.0)
        target:self selector:@selector(pollDisplay:) userInfo:nil repeats:YES];
    self.displayTimer.tolerance = 1.0 / 240.0;
    [[NSRunLoop mainRunLoop] addTimer:self.displayTimer
        forMode:NSRunLoopCommonModes];
}

- (void)stopTimer
{
    [self.workspace suspendMainPage];
    [self.songWindow suspendEditing];
    [self.helpWindow suspendEditing];
    [self consumeMidiStepCaptures];
    [self flushRuntimePublication];
    [self.displayTimer invalidate];
    self.displayTimer = nil;
}

@end

namespace {

bool init(const clap_plugin_t*) { return true; }

void guiDestroy(const clap_plugin_t* plugin);

void destroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    guiDestroy(plugin);
    delete instance;
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    return s3g::tracker::midi::activate(*self(plugin), sampleRate);
}

void deactivate(const clap_plugin_t* plugin)
{
    s3g::tracker::midi::deactivate(*self(plugin));
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    s3g::tracker::midi::reset(*self(plugin));
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    return s3g::tracker::clap_adapter::process(*self(plugin), processData);
}

void onMainThread(const clap_plugin_t* plugin)
{
    drainRetiredRuntimes(*self(plugin));
}

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    (void)isInput;
    return 1u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 50u : 100u;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "MIDI Record Input" : "Tracker MIDI Output");
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount,
    notePortsGet,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* instance = self(plugin);
    ProjectDocument document;
    if (instance->coordinator)
        document = [instance->coordinator currentDocument];
    else {
        std::lock_guard<std::mutex> lock(instance->documentMutex);
        document = instance->document;
    }
    std::string json;
    const auto result = s3g::tracker::encodeProjectDocument(document, json);
    if (!result.ok()) return false;
    std::size_t written = 0u;
    while (written < json.size()) {
        const int64_t amount = stream->write(stream,
            json.data() + written, json.size() - written);
        if (amount <= 0 || static_cast<uint64_t>(amount)
                > json.size() - written) return false;
        written += static_cast<std::size_t>(amount);
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    std::string json;
    std::array<char, 8192u> buffer {};
    for (;;) {
        const int64_t amount = stream->read(stream,
            buffer.data(), buffer.size());
        if (amount < 0 || static_cast<uint64_t>(amount) > buffer.size())
            return false;
        if (amount == 0) break;
        if (json.size() + static_cast<std::size_t>(amount)
            > s3g::tracker::kMaximumProjectDocumentBytes) return false;
        json.append(buffer.data(), static_cast<std::size_t>(amount));
    }
    ProjectDocument document;
    const auto result = s3g::tracker::decodeProjectDocument(json, document);
    if (!result.ok()) return false;
    auto* instance = self(plugin);
    if (instance->coordinator)
        [instance->coordinator cancelRuntimePublication];
    publishDocument(*instance, document, false);
    if (instance->coordinator) {
        [instance->coordinator applyDocument:document];
        [instance->coordinator resetHistory:document];
    }
    return true;
}

const clap_plugin_state_t stateExtension {
    stateSave,
    stateLoad,
};

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    if (!s3g::portable_gui::foundation::acquireRuntime()) return false;
#endif
    instance->coordinator = [[S3GTrackerClapCoordinator alloc]
        initWithPlugin:instance];
    if (!instance->coordinator) {
#if defined(S3G_TRACKER_VSTGUI_PILOT)
        s3g::portable_gui::foundation::releaseRuntime();
#endif
        return false;
    }
    NSView* view = instance->coordinator.pageView;
    view.frame = NSMakeRect(0.0, 0.0, kNativeWidth, kNativeHeight);
    instance->guiView = (__bridge_retained void*)view;
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    instance->guiContainer = [[S3GTrackerClapScaledView alloc]
        initWithContent:view nativeSize:NSMakeSize(kNativeWidth, kNativeHeight)];
    if (!instance->guiContainer) {
        CFBridgingRelease(instance->guiView);
        instance->guiView = nullptr;
        instance->coordinator = nil;
        s3g::portable_gui::foundation::releaseRuntime();
        return false;
    }
    const NSSize fit = s3g::clap_gui::responsiveViewportSizeForScreen(
        kNativeWidth, kNativeHeight,
        static_cast<uint32_t>(std::lround(kNativeWidth
            * s3g::portable_gui::foundation::kMinimumEditorScale)),
        static_cast<uint32_t>(std::lround(kNativeHeight
            * s3g::portable_gui::foundation::kMinimumEditorScale)));
    uint32_t width = static_cast<uint32_t>(fit.width);
    uint32_t height = static_cast<uint32_t>(fit.height);
    adjustTrackerSize(&width, &height);
    [instance->guiContainer setFrameSize:NSMakeSize(width, height)];
#else
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            view, kNativeWidth, kNativeHeight, kMinimumWidth,
            kMinimumHeight)) {
        CFBridgingRelease(instance->guiView);
        instance->guiView = nullptr;
        instance->coordinator = nil;
        return false;
    }
    view.frame = NSMakeRect(0.0, 0.0,
        instance->guiViewport.width, instance->guiViewport.height);
#endif
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance || !instance->guiView) return;
    [instance->coordinator stopTimer];
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    [instance->guiContainer removeFromSuperview];
    [instance->guiContainer.content removeFromSuperview];
    instance->guiContainer = nil;
    CFBridgingRelease(instance->guiView);
    instance->guiView = nullptr;
#else
    s3g::clap_gui::destroyResponsiveViewport(instance->guiViewport,
        instance->guiView);
#endif
    instance->coordinator = nil;
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    s3g::portable_gui::foundation::releaseRuntime();
#endif
}

// Cocoa supplies logical points and handles Retina backing scale itself.
// User zoom comes from set_size, not a second OS/DPI multiplier.
bool guiSetScale(const clap_plugin_t*, double) { return false; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    auto* container = self(plugin)->guiContainer;
    return container && s3g::clap_gui::portable::getSize(
        static_cast<uint32_t>(std::lround(container.frame.size.width)),
        static_cast<uint32_t>(std::lround(container.frame.size.height)), width, height);
#else
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kNativeWidth, kNativeHeight,
        width, height, kMinimumWidth, kMinimumHeight);
#endif
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    return s3g::clap_gui::portable::getResizeHints(
        kNativeWidth, kNativeHeight, hints);
#else
    return s3g::clap_gui::getResponsiveResizeHints(hints);
#endif
}

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    (void)plugin;
    return adjustTrackerSize(width, height);
#else
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kNativeWidth, kNativeHeight,
        width, height, kMinimumWidth, kMinimumHeight);
#endif
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* instance = self(plugin);
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    if (!instance->guiContainer || !adjustTrackerSize(&width, &height)) return false;
    [instance->guiContainer setFrameSize:NSMakeSize(width, height)];
#else
    if (!s3g::clap_gui::setResponsiveViewportSize(
            instance->guiViewport, width, height)) return false;
    if (instance->coordinator)
        instance->coordinator.pageView.frame = NSMakeRect(
            0.0, 0.0, width, height);
#endif
    return true;
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* instance = self(plugin);
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    if (!instance->guiContainer) return false;
    [(__bridge NSView*)window->cocoa addSubview:instance->guiContainer];
    return true;
#else
    return s3g::clap_gui::setResponsiveViewportParent(
        instance->guiViewport, (__bridge NSView*)window->cocoa,
        instance->host);
#endif
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    if (!instance->guiView || !instance->guiContainer) return false;
    instance->guiContainer.hidden = NO;
#else
    if (!instance->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
#endif
    [instance->coordinator startTimer];
    [instance->coordinator.pageView showPage:S3GTrackerClapPageTracker];
    [instance->coordinator.workspace focusTracker];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    [instance->coordinator disarmMidiStepRecording];
    [instance->coordinator stopTimer];
#if defined(S3G_TRACKER_VSTGUI_PILOT)
    instance->guiContainer.hidden = YES;
    return true;
#else
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
#endif
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide,
};

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.tracker",
    "s3g Tracker",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0",
    "Polymetric tracker, song sequencer, and sample-accurate MIDI generator.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->services = s3g::tracker::clap_adapter::hostServices(host);
    if (!s3g::tracker::midi::initialize(*instance)) {
        delete instance;
        return nullptr;
    }
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

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
