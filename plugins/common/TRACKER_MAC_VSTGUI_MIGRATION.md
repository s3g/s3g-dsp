# Tracker: Mac-first VSTGUI migration and Windows integration

Status: **all ten existing C++/VSTGUI pages and the portable shell now link into
a Windows x64 CLAP test build**. Tracker, Song, Geometry, Bursts, Phrases,
Assemble, Reshape, Warps, Console and Help reuse the Mac migration surfaces;
no replacement parameter panel was introduced. Native Windows/REAPER execution
and visual acceptance remain outstanding. Keep this separate from accepted
Windows release bundles until those checks pass. Mac remains on its existing
native hosting/coordinator adapter and opt-in portable surfaces.

## Playback row follow (2026-09-13)

The shared main page now has VIEW header controls for STATIC (unchanged default),
CENTER and PAGE, an independent NOTE-lane source menu, and explicit RESUME after
manual navigation/editing. CENTER/PAGE pin existing headers, preserve all
polymetric column cursors, and add a subtle row band/strong source outline.
CENTER permits blank boundary padding; PAGE uses aligned groups up to 16, with
the actual size shown (16 at 55% grid zoom, 8 at 100%, 4 at 180%). No grid height,
font size or outer-window resize policy was changed to make the groups fit.

The default source is pinned NOTE lane 1; SELECTED LANE is opt-in. Right-click
a lane name for FOLLOW THIS LANE. A pinned source travels with lane reordering;
removing an earlier lane remaps it, while deleting the source disables follow.
Across patterns the source is an ordinal slot; an absent slot shows NO LANE
without silently choosing another lane. Source and mode use optional version-3
session metadata and the shared document controller, so old documents default
to STATIC and default canonical encodings remain unchanged. Manual hold is
transient. Preference changes mark host state dirty without publishing a new
playback runtime. Scrolling reads the already-delayed NOTE snapshot: no new
clock, interpolation, note-on heuristic or MIDI scheduling change.

Validation:

- **51/51 targeted Mac tests pass**, including CLAP smoke, existing page parity,
  live-code Space routing and the new real-CFrame follow interactions. The known
  native-only `workspace_layout_appkit` baseline remains excluded as before.
- **11,541 pure follow checks** cover viewport geometry, project/host persistence,
  backward compatibility, malformed state rejection, undo/redo and every
  four-lane reorder mapping. These plus the coordinator (119 checks) and command
  controller (43 checks) also pass AddressSanitizer/UndefinedBehaviorSanitizer.
- Real-page tests cover centered boundary/reverse/stride/random/wrap jumps,
  adaptive pages, pinned/selected/missing sources, manual wheel/edit hold,
  RESUME, header hit testing, Song read-only source selection, the existing
  256-row/32-lane limit and latest-snapshot recovery. Repeated refreshes at
  15/30/60/120 calls do not advance playheads or publish musical edits.
- Windows x64 CLAP and 40 portable core test executables compile. Native Windows
  execution/REAPER acceptance remain pending. The refreshed 3 MB test package is
  `dist/s3g-tracker-windows-x64-test-20260913-181232.zip`; ZIP integrity and all
  packaged SHA256 checks pass. Prior test packages are preserved.
- Mac build signing verifies. The built executable SHA256 is
  `22f04da3f81090cfea07406d017136aa1ee9b9a380e6b81608528616caf4cf6e`;
  the packaged Windows CLAP is
  `5058ce4bdff2134e9cbf3ff19bcff00d9a13e2335249a49afb7dd40effa1b1d9`.
  **This step did not install the Mac build.** The installed executable remains
  `e3b44f9123e5eb27e090f9abe31233be0319f8493c48de67b4807f75688b55cc`.

Usage is documented in `docs/s3g-tracker.html#playback-follow`; the Windows README
now includes follow-specific REAPER acceptance checks. Actual Mac references
are captured by `S3G_TRACKER_MAIN_CAPTURE_DIR` in the portable main-page test.

Installation follow-up (2026-09-13): the tested follow build is now installed at
`/Users/s3g/Library/Audio/Plug-Ins/CLAP/s3g-dsp/s3g_tracker.clap`.
The installed executable matches the built SHA256
`22f04da3f81090cfea07406d017136aa1ee9b9a380e6b81608528616caf4cf6e`;
full bundle comparison and strict signature verification pass. The preceding
version is preserved at
`/Users/s3g/Library/Application Support/s3g-dsp/CLAP Backups/Tracker-follow-install-j3IoeJ/s3g_tracker.clap`.
REAPER was not running during installation. No other plugin was replaced.

## Windows CLAP hosting and coordinator (2026-09-13)

The Windows wrapper advertises the same `org.s3g.s3g-dsp.tracker` identity,
version 0.4.0, MIDI ports, CLAP state format and shared audio engine as Mac.
The root build now includes Tracker's CLAP directory on `APPLE OR WIN32`.
`plugins/clap_tracker/Windows.cmake` links all real page sources, the common
VSTGUI foundation and the portable engine. There is no alternate scheduler,
native audio-device dependency or restored standalone app.

`clap_editor_coordinator.h/.cpp` extracts the original bank, pattern, history,
command, preview, recording and Song publication policies into C++. All **53
WorkspaceCallbacks assignments** in the Mac coordinator are present. Song
edits still use stable row identities, next-row immutable handovers and deferred
publication after the final non-looping row. Runtime edit coalescing is a
main-thread deadline, not an audio clock. Stopped tempo queries and transport
requests use the existing CLAP/REAPER policies. Windows uses this coordinator;
the Mac coordinator is deliberately retained as the acceptance reference.

The Win32 adapter provides:

- Embedded HWND/CFrame ownership for the shell and all ten pages, proportional
  **65–200%** main-window resizing, and responsive detachable tool windows.
  Detached windows are owned by the REAPER FX top-level window, not globally
  always-on-top. Close reattaches the existing frame, preserving page state.
- Private IBM Plex Mono grid faces plus Fira Code for Windows suite controls;
  safe system-monospace fallback if resources are absent. Mac's Menlo is not
  copied or redistributed. Color roles/layouts come from the shared pages;
  CoreText/DirectWrite rasterization and display profiles still need visual
  comparison on the Windows machine.
- Module-relative `Resources/Fonts`, wide-character window names, shared file
  dialogs with `*.s3gt`/`*.s3gpack` filters, UTF-8/UTF-16 conversion and the
  already-tested Windows atomic file-store implementation. VSTGUI owns the
  clipboard and generic text editing; clipboard revisions use Win32's sequence.
- Instance-scoped REAPER accelerator and `hwnd_info` registration for editing
  focus, including Space in live code. Shared hooks are reference-counted and
  removed before views close; sibling instances keep their own registrations.
- A presentation-only timer reading the same audio-stamped mailboxes and
  existing one-frame delayed visual snapshot as Mac. Preview loops, MIDI gates
  and Song boundaries remain sample-clock driven even if GUI refresh stalls.

Validation performed on this Mac:

- **50/50 targeted Tracker tests pass**, including CLAP GUI/MIDI smoke and the
  existing page-parity regressions. The known five native-only
  `workspace_layout_appkit` baseline failures remain excluded, not fixed.
- New coordinator regression: **117 checks**, also passing under
  AddressSanitizer/UndefinedBehaviorSanitizer. Coverage includes all callback
  bindings, pattern edits/undo/redo, Unicode rename, Song mute recall, MIDI
  recording, quantized/deferred Song publication, cleanup, audio-stamped visual
  delay and host-BPM preview loop seams with no GUI polling. Legacy zero-ID Song
  rows acquire identities on first open, just as in the original Mac editor.
- The full Windows x64 plugin, **39 core regression executables** and a native
  Windows HWND/CLAP integration host compile and link. PE exports/dependencies
  are checked; GCC/libstdc++/winpthreads are statically linked. Windows system
  DLLs remain dynamic. **No Windows executable has been run here.**
- `.github/workflows/tracker-windows-clap.yml` now builds/tests native Windows
  core and GUI integration, plus Unicode module paths/missing-font fallback.
  This workflow was added locally, not dispatched or reported as passing.

`tests/tracker_windows_clap_smoke.cpp` loads the actual DLL and exercises ten
page tabs, state streams, sizing, detachment/re-attachment, both live-code fields,
REAPER-hook isolation across instances, cleanup/reopen and MIDI processing.
The REAPER bridge in that executable is a stub, so a pass still does not replace
the real-host checklist in `TRACKER_WINDOWS_README.txt`.

Reproduce the Mac cross-build (with the existing MinGW toolchain installed):

```sh
cmake --preset clap-windows-cross -DS3G_BUILD_TRACKER_PREVIEW=ON -DBUILD_TESTING=ON
cmake --build build-clap-windows-cross --target s3g_tracker_windows_clap_smoke s3g_tracker_portable_core_tests -j8
cmake -P scripts/package-windows-tracker.cmake
```

The packaging helper checks PE/CLAP exports and compiler-runtime dependencies,
creates a new timestamped folder/ZIP without replacing older packages, and
includes fonts, all notices, source hashes, payload checksums and the optional
Windows smoke launcher. Nothing is installed by building or packaging.
The rebuilt Mac executable is byte-identical to the previous shared-engine
milestone (`b3a4fec0...`); the installed Mac executable remains `e3b44f91...`.

Latest checked test artifact:
`dist/s3g-tracker-windows-x64-test-20260913-173555.zip` (about 3 MB), with the
same-named copyable folder. Packaged CLAP SHA-256:
`665d95396c86997a2429c40bd39bb88a4bde1714181325ac04f13ad8b7a02592`.
ZIP integrity and all packaged payload checksums pass. This supersedes the
earlier 17:33 package with cached Windows grid font metrics to avoid repeated
font metric queries per cell. It does not change the audio timing path.

## Shared CLAP MIDI engine and adapter (2026-09-13)

`clap_midi_engine.h/.cpp` now owns the production processing and publication
code formerly embedded in the Mac wrapper: initial document, activation/reset,
pending and quantized runtime swaps, document/preview publication, MIDI event
segmentation, channel routing, note identities and gates, live monitoring and
recording, panic, Burst/Pitch/Phrase/Assemble audition, and visual mailboxes.
The Mac plugin inherits this stable-address engine and delegates its lifecycle
and process callbacks directly to it; there is no alternate Windows scheduler.

`plugins/clap_tracker/s3g_tracker_clap_adapter.h` translates real CLAP transport
and MIDI events into borrowed, allocation-free core views and translates MIDI
back to CLAP. Beat/seconds fallback, tempo increments, port filtering, output
rejection counters, host process/callback requests and state-dirty ordering
retain the previous policies. Missing optional event/host callbacks are safe.
The engine requires neither CLAP headers nor a GUI SDK; the adapter requires
only CLAP headers. Runtime construction, superseded-pending deletion and retired
runtime reclamation remain on the main thread. A full retirement queue defers
handoff; the audio callback never frees a runtime.

New engine regression: **1,286 checks** pass, including 44.1/48/96 kHz,
fractional BPM, different block sizes, host start/stop, in-block transport
changes, exact recording offsets, shared-pitch monitor ownership, preview loop
seams without GUI ticks, Burst/audition gates, missing/rejected output, pending
and quantized publication, queue backpressure and concurrent publication.
Allocation/deletion guards and an AddressSanitizer/UndefinedBehaviorSanitizer
build of the extracted engine and test also pass. The CLAP adapter's **18
checks** cover actual ABI events and host callbacks.

A temporary differential probe against the pre-extraction Mac code passed
**27,220 exact MIDI-event comparisons** and **1,219,540 total parity checks**
across 72 scenarios (pattern/Song, sample rate, BPM and buffer size), including
tempo/position changes, previews, input, panic/resync and document handoffs.
It also compared playheads, audio-stamped visual hits and recording captures.
No graphics-FPS clock or change to the existing presentation-delay snapshot
was introduced.

Mac acceptance: **49/49 targeted Tracker tests pass**. The known native-only
`workspace_layout_appkit` baseline remains excluded, not fixed. All **38 core
regression executables**, including both new tests, compile/link for Windows
x86-64. Native Windows execution has not been performed here.

The existing native Mac/Windows core CI workflow now checks out the repository's
pinned CLAP 1.2.6 headers and supplies
`S3G_TRACKER_CLAP_TEST_INCLUDE_DIR` to include adapter tests without fetching
VSTGUI or building the full plugin. The workflow was updated, not run remotely.
For local core-only builds, set this optional CMake path to a directory
containing `clap/clap.h`; leaving it empty omits only the adapter test.

This milestone is built, **not installed**, at
`build-clap-sample-vstgui-fidelity/plugins/clap_tracker/s3g_tracker.clap`.
Its executable SHA-256 is
`b3a4fec0d6da7285d3d3b9ed6a22f547c0f7ab3d8db3797a0148fcc3b27e7062`;
strict bundle signing passes. The installation checkpoint below is unchanged.
Next: extract remaining page/bank coordinator policy and connect
portable page/window hosting, then Windows dialogs, clipboard, DPI/resources
and REAPER keyboard focus before enabling a full Windows CLAP target.

## Shared CLAP playback runtime (2026-09-13)

`clap_playback_runtime.h/.cpp` now owns the production runtime previously
embedded in the Mac Objective-C++ CLAP file. The Mac plugin uses this shared
implementation directly: this is not a parallel approximation or a test-only
runtime. It prepares the minimal pattern set, resolves host/project/Song tempo,
preserves row-local swing/loop/warp settings, applies Song transitions and
conditions, admits quantized runtime launches, and publishes the recording
clock and timestamped visual note hits. The runtime cannot be copied/moved
because the scheduler's logical-tick observer points to its stable address.

`RuntimeRetirementQueue` retains the existing 64-slot single-producer/single-
consumer handoff. A full queue defers the swap, preserving ownership rather
than allocating, deleting, or overwriting a live pointer on the audio thread.
The main thread drains retired runtimes. At that checkpoint the wrapper still owned the actual
runtime-pointer swaps, pending document publication, CLAP MIDI event delivery,
host callbacks and native GUI/window services. The subsequent MIDI-engine
milestone above extracts the processing/publication portion.

The new core regression exercises host-rate resolution, actual pattern/Song
sample timings, selected Song-row launches, finished-Song relaunch, all four
variation boundaries, recording offsets, note-hit/sample timestamps, queue
backpressure, concurrent retirement and allocation/deletion guards. Its **121
checks** pass on Mac and in an AddressSanitizer/UndefinedBehaviorSanitizer build
of the extracted code and test. A temporary differential probe against the
pre-extraction runtime passed **7,750 exact scheduled-event comparisons** over
240 combinations of pattern/Song mode, resync, sample rate, tempo and buffer
size, including host tempo changes, swing and Song loops. The probe also
compares visual timestamps, recording targets and Song row/repeat state.

All **36 core regression executables** compile/link for Windows x86-64,
including this production runtime. They remain included in the native Windows
CI aggregate; Windows execution and a Windows Tracker CLAP have not been
validated here. No GUI-FPS clock, display extrapolation, scheduler redesign,
or change to the existing presentation-delay snapshot was introduced.

Mac acceptance: **47/47 targeted Tracker tests pass**, including the real CLAP
MIDI smoke, Phrase/Assemble loop regression and all portable page tests. The
previously documented native-only `workspace_layout_appkit` baseline remains
excluded, not fixed. Bundle signing, documentation and whitespace checks pass.

The planned CLAP process/MIDI-delivery and runtime-publication extraction is
now complete (see above). Windows page/window hosting, dialogs and REAPER text
focus remain; a Windows test plugin/package is not ready yet.

### Mac installation checkpoint

The tested Phrase/Assemble loop-fix build is now installed at
`/Users/s3g/Library/Audio/Plug-Ins/CLAP/s3g-dsp/s3g_tracker.clap` with executable
SHA-256 `e3b44f9123e5eb27e090f9abe31233be0319f8493c48de67b4807f75688b55cc`.
Strict signing and complete source/installed bundle comparison pass. Its
predecessor is preserved intact in
`/Users/s3g/Library/Application Support/s3g-dsp/CLAP Backups/Tracker-phrase-loop-20260913-Xdebp9sA/s3g_tracker.clap`.
No REAPER process was quit or project changed. Restart REAPER to load it.
The subsequent shared-runtime refactor is built separately, not installed;
its executable SHA-256 is
`729dfd2aabda0f1bb505a2339a83d267cf55a2d7941e47c47ac0607f35a68b5e`.

## Audio-clock Phrase/Assemble LISTEN loops (2026-09-13)

The portable authoring pages now publish one immutable preview plan with an
explicit full duration. Leading/trailing rests and assembly repeats remain in
the musical timeline. `PreviewSequencer` schedules every repeat on the CLAP
sample clock, follows valid host tempo updates (including linear tempo ramps),
and carries fractional row timing rather than rounding and accumulating each
row. Missing host tempo uses the BPM captured when LISTEN was requested.
The 33 ms GUI timer only reads the audio-owned row; it cannot restart MIDI.
This replaces the previous row-duration timer, which could add roughly a row
of silence at non-integral BPMs or after a GUI stall.

LISTEN stop/hide/recall and host start/panic/reset cancel audio playback.
Owned tokens keep an old page from stopping a newer audition. Preview note-offs
are merged chronologically with onsets, including repeat seams; stopping
releases preview notes without releasing unrelated MIDI-monitor keys.
Pitch Map keeps its one-shot callback, but shares the immutable-plan engine.
The old 256-event truncation is gone, and assembly preview rows no longer wrap
at 65535. Plan allocation, sorting and reclamation happen on the main thread;
audio processing allocates/frees nothing. Native Cocoa authoring views remain
legacy comparison references; this correction targets the portable pages.

Regression coverage includes 128 uninterrupted loops at 90/97.5/120/123/130/
145.5 BPM, 44.1/48/96 kHz, 64/127/512/8192-frame blocks, sub-row events, leading
and trailing rests, mid-loop tempo/loop changes, cancellation ownership,
long/dense assemblies, concurrent publication and audio allocation checks.
The CLAP smoke test clicks LISTEN in both pages, renders MIDI without GUI ticks,
and checks host-BPM onsets, routing, note-off ordering and cancellation.
The UI test also misses 100 repeat boundaries without republishing MIDI.

Acceptance: **46/46 targeted Mac tests pass**, excluding the previously
documented native `workspace_layout_appkit` baseline. The preview engine's
**37,172 checks** also pass with AddressSanitizer/UndefinedBehaviorSanitizer;
measured onset error stays within one sample, with no accumulated loop drift.
The CLAP test accepts the supported screen-fitted initial size before explicitly
setting its reference size, avoiding dependence on the current NSScreen.
All **35 core test executables** cross-compile/link for Windows x86-64; Windows
runtime execution and the Windows Tracker CLAP remain pending. Strict Mac
bundle signing, documentation and whitespace checks pass.

The Mac executable SHA-256 at this milestone was
`e3b44f9123e5eb27e090f9abe31233be0319f8493c48de67b4807f75688b55cc`.
It was subsequently installed at the checkpoint above.

## Portable CLAP commands + file storage (2026-09-13)

The Mac CLAP now delegates its live-code command policy to ordinary C++ in
`clap_command_controller.cpp`: MIDI-only command restrictions, Help/actions,
bank-wide phase reset, whole-project Burst reference counting, variation
admission/RNG rollback, undo/redo routing and ordered command effects. The
synchronous service boundary preserves the original order of publication,
host transport requests, panic, monitor update and UI refresh. It does not
move playback scheduling, the visual-delay snapshot, timers or audio-thread
mailboxes onto GUI frames. Variation installation and Song/runtime handover
remain in the native coordinator.

The project/asset-pack codecs now share bounded file storage. The POSIX
implementation retains its existing same-directory temporary, file fsync,
rename and parent fsync behavior, including permission-bit preservation.
The Windows implementation uses strict UTF-8/UTF-16 conversion, wide absolute
paths (including UNC/extended-length paths), exclusive temporary creation,
complete writes, `FlushFileBuffers` and same-volume replacement with
`MoveFileExW`. There is no delete-destination or cross-volume-copy fallback.
See Microsoft's [replacement/write-through flags](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
and [buffer flushing contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).
Crash durability still depends on the filesystem/device; no network-share
durability guarantee is made. Windows permissions follow the temporary file's
inherited directory ACL rather than a POSIX mode-bit translation.

Mac asset-pack dialogs now use the shared store instead of NSData file I/O.
Export still captures the pack when opening the dialog; import still applies
only a fully decoded/validated pack. Project and pack reads are bounded to
64 MiB and failed loads leave destination models unchanged. The dialogs
themselves, their ownership and the remaining bank-management UI are still Mac
adapters, not yet Windows-hosted controls.

The full shared core and all **34 core regression executables** compile and
link as Windows x86-64 binaries in `build-tracker-windows-core/`. The storage
executable imports the expected wide-path/flush/replacement Windows APIs;
these are not merely syntax-only page checks. Windows execution is still
pending: this Mac has no Windows runtime, and the added
`.github/workflows/tracker-portable-core.yml` has not been run remotely. That
workflow builds and runs the same core tests on native macOS and Windows,
without fetching VSTGUI or enabling a Windows Tracker CLAP. Native Windows
tests include Unicode and extended paths, sharing/read-only failures, old-file
preservation and temporary cleanup. Existing MinGW conversion/shadow and
sequencer-optimization warnings are not claimed resolved by this extraction.

Mac acceptance: **45/45 targeted Tracker tests pass**, including **40 new CLAP
command checks**, **17 filesystem checks**, the existing codec/history/timing
suite and all portable GUI/MIDI/parity regressions. The previously documented
native-only `workspace_layout_appkit` baseline remains excluded, not fixed.
Strict bundle signing, documentation validation and whitespace checks pass.
The rebuilt Mac executable SHA-256 is
`137317e628ef4b7f77a8f3f17823e336ba942ed23ef925ac66660adcf4809551`.
The installed executable remains `38274dcb…`, the compact-submenu checkpoint
below. No installation or Windows plugin package was made in this milestone.

## Compact Tracker context submenus (2026-09-13)

Child menus now size themselves independently of their parent's width, using
the actual rendered uppercase label widths in the resolved menu font. Leaf
items have nine-point horizontal margins; items with children reserve another
six points for the existing arrow. There is no inherited 132-point child
minimum. Top-level menus/dropdowns, row heights, colors, per-item separators,
selection markers and actions are unchanged. The existing 610-point cap and
text fitting still protect against exceptionally long names.

The portable-main GUI regression exercises nested percentage menus, text-fit
widths against independent Cocoa font measurements, pointer action dispatch,
Left/Escape navigation, Unicode and oversized Phrase names, and opening to the
left at the right window edge. Captures were inspected in
`build-tracker-vstgui-pilot/compact-submenus/`. The modified portable menu source
also passes MinGW C++17 syntax checking.

All **43 targeted Tracker tests pass** with the submenu refinement; the
previously documented native-only baseline remains excluded. The rebuilt
bundle, including the portable shell/controller milestone below, is now
installed at
`/Users/s3g/Library/Audio/Plug-Ins/CLAP/s3g-dsp/s3g_tracker.clap`.
Strict signature verification and a complete source/installed bundle comparison
pass. Its executable SHA-256 is
`38274dcb38673cad834b16093c88dbd990de39d6af1e5bfbfad8ba5df78a4f9d`.
The previous all-pages bundle (`81d5b33e…`) was moved intact to
`/Users/s3g/Library/Application Support/s3g-dsp/CLAP Backups/Tracker-compact-submenus-20260913-bpbJUZdm/s3g_tracker.clap`.
No REAPER instance was quit or project modified. Restart REAPER to load the
replacement; user review is the next step before further portability work.

## CLAP-only shell/controller milestone (2026-09-13)

Tracker is now exclusively a MIDI CLAP product. Its standalone app entry point,
build target and packaging were already absent. This pass removes the remaining
unused CoreMIDI/audio-device service headers, inert device-selection callbacks,
unused internal synth/engine adapters and their app-only audio-contract test
(ten tracked files). Git retains those files for recovery. Shared rack/sampler
schema types, codec compatibility, CLAP resources and independent Cocoa parity
references remain: deleting those would break the current plugin or its tests.
The No Input Mixer standalone app is unrelated and is unchanged.

`S3G_ENABLE_TRACKER_PORTABLE_SHELL_ON_MACOS=ON` enables the outer shell and all
ten portable pages. It remains an opt-in Mac validation build. `ShellController`
owns page ordering, selection/wrapping, detach eligibility/state, header/content
layout and status formatting. `ShellView` paints and hit-tests the tabs,
MIDI-event/HOST-BPM readouts, detach button and detached-page placeholder in
VSTGUI, following the original Cocoa header geometry, typography and colors.
The Mac host still owns actual NSWindows and the page CFrame adapters; it does
not recreate hidden Cocoa tab buttons to handle clicks. The CLAP test now
delivers real pointer events to the portable header. The Mac adapter exposes
accessible tab/detach actions and status readouts through the same portable
hit/action path. This is not a claim of full OS accessibility parity. Page
shortcuts yield to focused text entry, and window reparenting is deferred out
of the active CFrame pointer dispatch.

`ClapDocumentController` extracts active-pattern/asset-bank synchronization,
MIDI-only normalization, persistent snapshot capture/apply and history ownership
into ordinary C++. The native adapter still supplies the current Song and
applies the returned pattern catalog before Song rows, preserving saved mute
masks. Undo/redo still cancel pending publication before replacing the document.
This extraction does not change scheduling, the delayed visual snapshot,
publication timers or the audio-thread mailboxes. Remaining coordinator work
includes command/asset-pack actions, Song handover and runtime publication.

The integrated smoke exposed a pre-paint Warps hit-map race after detaching:
the view had resized to 920 points, but its numeric editor targets could still
reflect the old 1320-point layout until the next paint. Warps now rebuilds its
hit targets synchronously on resize/reload using the same layout routine as
painting. The regression test edits a numeric field before that first repaint;
the fix does not add a sleep or change playback timing.

Validation on the development Mac:

- All **43 targeted Tracker tests pass**, including CLAP GUI/MIDI integration,
  all seven portable page/shell parity targets, project/pack codecs and timing
  regressions. The known native-only `workspace_layout_appkit` baseline remains
  excluded; its five previously recorded failures below are not claimed fixed.
- The new document/controller test passes **91 checks**. The shell test passes
  **11 checks**, including real pointer and accessible tab actions. Decoded,
  nonblank Cocoa/VSTGUI captures have zero differing pixels in the checked tab
  and status regions. This scoped comparison excludes the detach glyph, which
  follows the already-approved cap-height centering convention.
- CLAP integration and Warps parity also pass two consecutive runs each after
  the pre-paint fix. The final complete suite was then rebuilt and rerun.
- MinGW C++17 syntax checks pass for the new shared shell/document controller,
  shell view, modified Warps view and controller tests. This is not a Windows
  link or runtime test.
- Documentation checks and strict Mac bundle signature verification pass.
  The new build executable SHA-256 is
  `2f9dbc99e15e5b70a20732d48db824ebc417cb7f2f155231abe58ffc9c33edf3`.

At completion this milestone was built separately from the then-installed
all-pages checkpoint below; building alone did not install it.
Windows hosting, durable file I/O,
remaining platform adapters and a linked/runtime-tested Windows CLAP remain
unfinished; no standalone audio-device or Audio Unit port is needed.

### Earlier all-pages Mac installation (2026-09-13)

Installed the tested all-ten-pages CLAP bundle into
`/Users/s3g/Library/Audio/Plug-Ins/CLAP/s3g-dsp/s3g_tracker.clap` at the user's
request. The complete installed bundle matched that checkpoint's build (`diff -rq`), strict
signature verification passes, and its executable SHA-256 is
`81d5b33ed8f3d413582da91e87f2af015763ff14d90f03345d14783850104af5`.
The previous, user-confirmed Spacebar-fix bundle is recoverable at
`/Users/s3g/Library/Application Support/s3g-dsp/CLAP Backups/Tracker-20260913-Wf0lOaXC/s3g_tracker.clap`.
No host was quit or project modified. Restart REAPER to load the replacement.

## Portable Phrases + Assemble + Reshape milestone (2026-09-12)

`S3G_ENABLE_TRACKER_PORTABLE_AUTHORING_PAGES_ON_MACOS=ON` enables the final
three primary pages and the Geometry/reference/Song/Warps/main/pilot
prerequisites. Options remain OFF by default; the Mac migration test build
enables them. The original three Cocoa implementations are unchanged and
compiled independently into the parity test.

`editor_authoring.h`, `editor_phrase.cpp`, `editor_assemble.cpp` and
`editor_reshape.cpp` own document operations, parsing, typed clipboard
transactions, placement and immutable audition plans. `AuthoringPageView`
and its three painting units own the controls, drawing, menus, selection,
scrolling, keyboard entry and drag behavior. The Mac adapter supplies CFrame
lifetime, font metrics/ColorSync, focus, clipboard change count and the
existing Phrase/Assemble key-equivalent protocols. It does not replay Cocoa
drawing or forward editing to the native controllers.

Adaptation inventory, checked against the current source and the Phrase
section of `docs/s3g-tracker.html`:

- **Phrases:** all seven NOTE/VOL/SEQ1/V1/SEQ2/V2/GATE fields and eight headers,
  exact column/row geometry, semantic colors and font weights; cursor/range/
  audition states; drag and Shift-click selection, Control-Shift-Arrows,
  Control-A/C/X/V, arrows/Tab, direct typing and double-click editing. Typed
  clipboard cells retain full precision and reject incompatible destination
  columns; external TSV paste is atomic, supports selection fill and normalizes
  CR/LF. All note states, chords, bank-qualified Bursts, normalized/polyphonic
  values, gates, SEQ/CC menus and named CD conditions remain available. Library
  bank/slot, UTF-8 name, optional 20–400 BPM, length, save/duplicate/delete,
  import/export-one/export-bank/copy-project/clear-bank/delete-bank, MIDI
  audition channel/loop, main-page capture, replace/merge placement and status
  remain wired to the original document callbacks. Name/BPM edits remain
  drafts until explicit SAVE, matching Cocoa; leaving the field is not an
  implicit metadata save.
- **Assemble:** vertical variable-height phrase blocks, rounded selection
  outlines, row numbers, audition cursor and drop indicator; multi-selection,
  drag reorder/Option-copy, up/down/duplicate/delete/clear; palette bank/slot/
  repeat and A-to-append; selected-phrase/whole-assembly audition, MIDI channel,
  loop and Space; save as project Phrase, target pattern/lane/row, replace/merge,
  extend/crop/wrap-at-256, placement feedback and reveal in Tracker. In-progress
  drags do not mutate the draft until release; recall cancels them.
- **Reshape:** the original HITS/MT/VEL/LANE profile, original/variant contours,
  point markers, scales, empty-state hints and published playback cursor;
  target pattern, toggled multi-lane scope, auto/explicit cycle, analysis/result
  text; every mutation/timing/dynamics slider and write/outlier menu, reseed and
  protected-anchor label; non-destructive preview, Original/Reshaped comparison,
  reset, apply-in-place and create-variant callbacks. Mutation reuses existing
  bank-qualified Burst definitions; it does not generate a new asset library.

Presentation decisions are explicit: suite-style canvas menus replace the
remaining native SEQ context menus, preserving every item and CC grouping,
with a line between items. Scroll views use portable overlay scrollers plus
wheel/trackpad scrolling. Generic VSTGUI text fields inherit the confirmed
REAPER priority text-routing fix; seeded grid entry moves its caret to the end
instead of leaving the seed selected. Buttons use the approved cap-height
centering rule. Font metrics use the resolved native point size (Tracker's
IBM Plex font helper can enlarge the requested size), not just its input.
Menus and toolboxes do not gain unintended contrasting outlines. Full OS
text-service/accessibility parity is not claimed.

Embedded pages inherit the existing proportional 65–200% editor resize.
Detached pages reflow normally and fit proportionally below their usable
authoring size, preserving the lower control rows. The Reshape adapter keeps
a strong reference to its page independently of its original NSWindow, so
reparenting cannot orphan refresh or preview cleanup.

Audition events still enter the original immutable audio-thread mailbox.
Initially the dedicated audition timer controlled looping and its cursor used
elapsed monotonic time; paint/workspace refresh never emitted MIDI. This was
insufficient at repeat boundaries. The audio-clock correction above supersedes
that implementation: the GUI now only reads progress, while hide, close, recall,
bank/slot changes and transport start cancel the owned audio plan.
Reshape preview is cleared on hide/close or pattern switch and before apply;
reentrant host-error reloads cannot recursively republish the preview. Song
follow guards placement and Reshape commits. Saving an Assembly into project
Phrases preserves the previously active bank's edited library.

Validation targets: `s3g_tracker_editor_authoring_tests`,
`s3g_tracker_portable_authoring_tests`, and the extended
`s3g_tracker_clap_smoke`. The native comparison checks control geometry,
rendered authored regions, parsers, exact audition events and Reshape analysis.
The CLAP test uses native pointer/key delivery at 65/100/150/200%, serializes
Phrase names containing spaces, exercises canvas menus, detaches all three
pages, edits in a 480×360 window and reattaches without replacing the page.
Set `S3G_TRACKER_AUTHORING_CAPTURE_DIR` for paired independent Cocoa/VSTGUI
PDF/PNG references; the current capture folder is
`build-tracker-vstgui-pilot/portable-authoring`.

Final acceptance: **42 targeted Tracker tests pass**, including 56 authoring
core checks and 145 independent Cocoa/portable GUI checks. The latter include
explicit metadata SAVE, invalid BPM preservation, scrollbar interaction,
Option-drag copy, stale-drag recall and audition behavior at 30/60/120 refresh
FPS and after a display stall. The checked authored Phrase and Assembly raster
regions have no pixels above the comparison threshold; Reshape's mismatch is
0.0121%, concentrated at antialiased edges. This does not claim whole-window
pixel identity or OS text-service parity. As before, the native
`workspace_layout_appkit` test is excluded for its five documented baseline
failures; it is not counted as a pass.

MinGW accepts the three core authoring units, shared tool/authoring surface,
all three page drawing units and the authoring core test with the pinned
Windows VSTGUI headers (`-fsyntax-only`). This is compile-only evidence, not a
linked Windows plugin. The Mac CLAP build, strict bundle signature check and
`git diff --check` pass.

Building this milestone **does not install it**. At completion on September 12,
the user-confirmed installed Spacebar-fix bundle was unchanged; the subsequent
September 13 installation is recorded above. The Mac bundle is built at
`build-clap-sample-vstgui-fidelity/plugins/clap_tracker/s3g_tracker.clap`.
Its executable SHA-256 is
`81d5b33ed8f3d413582da91e87f2af015763ff14d90f03345d14783850104af5`;
the previous installed executable was
`3f93f4c69b0816803420af38497b7684a55d8a88a191fd148b9da7822329585b`.

Windows work still includes the CLAP GUI/window shell and detached windows,
coordinator/platform boundaries, file dialogs and durable UTF-8/UTF-16 file
operations, native audio/MIDI/instrument services where applicable, resource
packaging and a linked Windows/REAPER validation pass. Portable page compilation
alone does not verify those services, host shortcuts or real display latency.

## Portable Geometry + Bursts milestone (2026-09-12)

`S3G_ENABLE_TRACKER_PORTABLE_GEOMETRY_PAGES_ON_MACOS=ON` enables this pair and
the reference/Song/Warps/main/pilot prerequisites. All migration options remain
OFF by default; the current Mac test build enables them. The original Cocoa
`S3GTrackerGeometryView` remains intact and separately compiled in parity tests.
This build does not replace the installed, user-confirmed keyboard-fix bundle.

The shared `GeometryEditor` is ordinary C++: layout, state, all six ring views,
Pitch Map, the Burst matrix/radial/breakpoint displays, hit testing and edit
gestures. Its `DisplayList` includes closed filled markers, open polylines,
dash patterns and round caps. `GeometryPageView` supplies the VSTGUI surface,
canvas menus, generic Burst-name editor and separate audition timer. The Mac
host supplies CFrame lifetime, exact native fonts/metrics, ColorSync and focus;
it does not replay native drawing or forward authoring gestures to Cocoa.

The native reference determines the actual appearance, not just intended API
semantics. In particular, Cocoa's `NSFrameRect` uses the current **fill** color,
despite historical `setStroke` calls immediately before it. Panel, button,
slider and collapsed-menu frames therefore remain visually borderless. Real
Bezier borders, note flags and per-item dropdown separators are retained.
Uppercase action titles use the centered cap-height rule established in Warps.
The Burst name field retains its native Control/Border/TextPrimary colors.
Detached windows below the usable 960×720 authoring area fit proportionally,
so the original matrix/radial pair and lower placement rows remain reachable
instead of overlapping or clipping. Ordinary-size layouts remain unchanged;
embedded pages still use the CLAP parent's 65–200% scale.

Explicit safety corrections relative to native: Burst radial hit tests use
the painted center (no four-point offset); Song-follow blocks keyboard/radial/
toolbox mutations consistently; cancelled gestures restore only their own
pattern/bank, while document reload never writes a stale snapshot. Audition
stops on hide, close, reload, bank/slot changes, NEW/DUP and transport start.
Duplicating a maximum-length name preserves the valid name without adding an
over-limit suffix. Playback cursors require matching bank **and** Burst slot.

Musical time still comes from the existing coordinator/audio snapshots.
Neither paint nor refresh emits MIDI. Audition alone owns a row-duration timer;
its events still enter the existing immutable preview callback. Read-head halos
decay with monotonic elapsed time (140 ms half-life), including long display
stalls rather than clamping elapsed time to one quarter second.

Validation is in `s3g_tracker_editor_geometry_tests`, the independently native
`s3g_tracker_portable_geometry_tests`, and `s3g_tracker_clap_smoke`. Coverage
includes every mode's layout/radii, all menu contents, Burst authoring actions,
UTF-8 names, generic keyboard focus/Space, canvas menu keyboard navigation,
ring painting/Option erase/velocity/reveal, drag cancellation, Pitch Map's
non-note cells/polyphony, bank callbacks, audition lifetime and 30/60/120-FPS
elapsed-time tests. The actual CLAP exercises 65/100/150/200% native pointer
input, page switching, independent detached ordering and reattachment, and
retains the main/Console REAPER-priority text-routing regression.

Set `S3G_TRACKER_GEOMETRY_CAPTURE_DIR` for paired native/VSTGUI PDF and PNG
references; current captures are in `build-tracker-vstgui-pilot/portable-geometry`.
Native layer-backed cache bitmaps can be blank, so the test renders
both independent vector captures to PNG. Diagrams and controls are compared
against the original, with only the explicit corrections above. Full OS text
services/accessibility parity is not claimed: the Burst name is a generic
VSTGUI editor and canvas controls do not recreate hidden native controls.

The Windows compiler accepts all five Geometry core translation units, the
VSTGUI page and core tests. This is compile-only evidence, not a linked or
runtime-tested Windows Tracker plugin. Phrases, Assemble, Reshape and the
remaining platform services still prevent claiming a complete Windows port.

Final Mac acceptance: all **40 targeted Tracker tests pass**, including the
new pair, CLAP state/MIDI/GUI tests and previous main/Warps/Song/Console/Help
regressions. `workspace_layout_appkit` remains excluded for the five previously
documented native baseline failures; it is not counted as a pass. Strict bundle
signature verification and `git diff --check` also pass.

The extended smoke sequence exposed an intermittent detached-Warps assertion:
it serialized immediately after Return, whose commit is deferred to workspace
refresh. The test now draws the resized controls before clicking and observes
the queued commit for up to 500 ms, without replaying input. Temporary tracing
was removed; no Warps shipping behavior changed. The final complete run is green.

Ready-to-install Mac bundle:
`build-clap-sample-vstgui-fidelity/plugins/clap_tracker/s3g_tracker.clap`.
Geometry + Bursts have **not** been installed by this milestone; the installed
executable remains the user-confirmed `3f93f4c6…9585b` keyboard-fix build.

## Portable Console + Help milestone (2026-09-12)

Installation / follow-up: Console + Help were installed on 2026-09-12, after
backing up the previous Song milestone to
`~/Library/Application Support/s3g-dsp/CLAP Backups/tracker-console-help.D1aG2N/`.
The first Space-key fix (installed executable SHA-256
`5d05a548f2b0f2a008eebed7ebdbe3ca9c1edf7dc61a9b74ee1cb50132ba5e7e`)
was insufficient: direct Cocoa key-equivalent tests passed, but the user still
observed REAPER starting transport. A process-targeted CGEvent test reproduced
the failure in both embedded Live Code fields and detached Console. Sending
events directly to keyDown, performKeyEquivalent or NSApplication's queue did
not cover REAPER's earlier shortcut pretranslation.

The correction is in the Mac CLAP hosting layer, not the portable editor or DSP:

- Register REAPER's `hwnd_info` text-field/global-shortcut classification hook
  and a **priority `<accelerator`** hook. Ordinary `accelerator` registration
  runs too late for the built-in FX-chain Space shortcut.
- Return `-10` (raw macOS processing) only when the original message target
  belongs to a visible Tracker page with both VSTGUI text-edit focus and native
  first-responder ownership. REAPER child windows need not be AppKit's key
  window; matching the actual event target is essential.
- Let the native input context handle editing, selection, IME, clipboard and
  modifiers. The callback neither synthesizes keys nor changes transport.
  The prior scoped Space key-equivalent handler remains a Cocoa fallback.
- Share the text classification callback across instances, register each
  accelerator independently, and unregister before destroying its pages.
  Foreign windows, hidden pages and non-text focus retain REAPER's shortcuts.

These contracts use the
[REAPER extension SDK](https://github.com/justinfrankel/reaper-sdk/blob/main/sdk/reaper_plugin.h).
There is no upstream VSTGUI patch, process-wide event monitor, swizzle or new
playback clock. Windows input is unchanged.

Regression checks cover priority registration, shared hook lifetime, foreign
and null message targets, native Space insertion, selection replacement and
release of text ownership. The actual CLAP smoke invokes the registered host
hooks before native text delivery, including 65/100/150/200% scaling, shared
main/Console drafts and detached Console. This complements, rather than
substitutes for, the actual REAPER integration test described below.

Final validation on macOS arm64 / REAPER 7.78: both FX-chain and floating-window
runs pass with the global Space fixture enabled, including main Live Code,
Console Live Code, detached Console and the outside-text transport control.
The unfixed build fails the same real keyboard path. Before/after logs are in
`build-tracker-vstgui-pilot/keyboard-regression/`. All 38 targeted Tracker tests
also pass; `workspace_layout_appkit` remains excluded for its documented five
pre-existing Cocoa failures, not counted as a pass.

The corrected bundle is installed at
`~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/s3g_tracker.clap`.
Installed executable SHA-256:
`3f93f4c69b0816803420af38497b7684a55d8a88a191fd148b9da7822329585b`.
Strict signature verification and source/installed bundle comparison pass.
The previous failed-fix bundle is recoverable from
`~/Library/Application Support/s3g-dsp/CLAP Backups/tracker-reaper-text-input.Ojlu1L/`;
the earlier Console/Help bundle remains in `tracker-live-code-space.EZQu8s/`.
The user's running REAPER project was not closed or modified; fully restart
REAPER to load the replacement binary. Only isolated test instances were closed.

### Real REAPER keyboard regression harness

`tracker/tests/reaper_live_code_keys.mm` is a manual test extension, never a
shipping plugin resource or a user-profile installation. It refuses to run
unless REAPER's resource directory exactly matches the compile-time
`S3G_KEY_TEST_RESOURCE` path. Compile it as an ARC Objective-C++ dynamic library
with `-framework Cocoa -framework ApplicationServices -I tracker/include`,
defining that macro to a freshly created isolated `/private/tmp` directory,
and place it only in that directory's `UserPlugins` subfolder. Point the
temporary `reaper.ini`'s `clap_path_macos-aarch64` at the build's Tracker folder.
Launch REAPER with `-newinst -nosplash -cfgfile <temporary-dir>/reaper.ini`.
Never use the normal REAPER configuration or an existing user project.

The fixture creates an empty muted track, loads Tracker, and posts key down/up
CGEvents only to its own PID. It checks exact draft contents and host play state
after Space and Shift-Space in both embedded fields, then Space in detached
Console. A final negative control focuses REAPER and verifies Space still starts
transport there. Page navigation and pointer focus use native view APIs; the
key path itself is not a mocked call into the plugin. `S3G_KEY_TEST_GLOBAL=1`
also registers a global Space binding; `S3G_KEY_TEST_FLOATING=1` uses a floating
FX window instead of the default FX chain. The fixture writes `keys.log` and
leaves only its isolated instance open for inspection. Run GUI tests serially
to avoid focus interference, and terminate only the validated test PID.

### Portable page implementation

`S3G_ENABLE_TRACKER_PORTABLE_REFERENCE_PAGES_ON_MACOS=ON` enables Console and
Help, and the Song/Warps/main/pilot prerequisites. All five options default OFF.
The standalone app and separately compiled original Cocoa controllers remain
the independent adaptation references.

The pages preserve the current `s3g_tracker_workspace.mm` Console and the whole
`s3g_tracker_help_window.mm` reference, not a shortened or redesigned document:

- Console's prompt, command field, output panel, typography, normal/error colors,
  exact message prefixes, multiline/UTF-8 output, and append-follow scrolling.
- Shared main/Console drafts, 100-command history, Up/Down draft restoration,
  native completion order and ambiguity messages, Return execution, and Escape
  returning to the main page. Commands still enter the existing coordinator once.
- The original 30,000 UTF-16-unit log threshold and 10,000-unit prefix trim.
  Trimming deliberately preserves whole Unicode scalars at a surrogate boundary.
- Every command section, syntax, description, example and sequencing action,
  plus all four manual workflow guides, byte-for-byte against native Help text.
- Rich document colors and weights, paragraph/line spacing, tracking, guide
  indentation, word wrapping, read-only text selection/copy, mouse/keyboard
  scrolling, drag autoscroll, and Find/next/previous/wraparound.
- Independent detached-window reflow and existing top-level ordering/reattachment;
  embedded pages inherit 65–200% proportional CLAP magnification.

`editor_reference.h/.cpp` owns the platform-free Console model, shared completion
table, Help document, Unicode wrapping and selection geometry. The manual guide
strings live in `editor_help_guides.inc`; the native original remains independent
so tests detect drift. `ReferencePageView` owns portable painting, selection,
scrolling, canvas context menus and generic text input. The Mac host supplies
font/fallback metrics, ColorSync conversion, CFrame ownership and window focus.
Tracker retains its bundled IBM Plex Mono document font and Menlo suite headers;
this milestone does not substitute the general family Fira Code font.

There is no new playback clock or MIDI publication path. The existing workspace
refresh only observes Console revisions/shared drafts. Help lays out on document
or width changes. A selection-only autoscroll timer cannot advance playback;
hide/cancel/close releases input and drag state, including the Help find editor.

Presentation differences are explicit: native system scrollers and the find bar
are replaced by portable overlay scrollers and suite-styled Find controls; the
context menu follows the suite's per-item separator rule. Console output is kept
inside its viewport instead of copying the native capture's left-edge clipping.
The native field's border is retained even though layer-backed borders can be
absent from PDFs. Full OS text-service/accessibility parity is not claimed.

Validation targets are `s3g_tracker_editor_reference_tests`,
`s3g_tracker_portable_reference_tests` and the extended `s3g_tracker_clap_smoke`:

- Native/portable Help text equality and all 23,592 glyph positions at 1320,
  760 and 570 point page widths; font faces, sizes and colors are compared too.
  Console fixtures include multiline text, errors, accented Latin, Japanese and
  supplementary-plane characters; fallback line metrics retain native baselines.
- Real frame keyboard/pointer tests cover submission, history, completion,
  shared draft restoration, selection/copy, read-only output, Find, scrolling,
  canvas menus, trimming, reparenting and command callbacks after reattachment.
- Actual CLAP tests execute/serialize Console commands at 65/100/150/200%, share
  drafts with main Live Code, retain history across detach, and verify complete
  Help copy/search and Escape reattachment in a detached window.
- MinGW x86-64 accepts the reference model, reference page, shared tool controls,
  updated main page and reference core tests with `_WIN32`. This is compile-only
  evidence, not a linked or tested Windows Tracker plugin.
- All 38 targeted Tracker tests pass, including main/Warps/Song regression tests.
  As in prior milestones, this excludes `workspace_layout_appkit` and its five
  documented baseline Cocoa failures; the broader native suite is not claimed green.

Set `S3G_TRACKER_REFERENCE_CAPTURE_DIR` to produce paired native/VSTGUI PDF and
bitmap references. Current captures are in
`build-tracker-vstgui-pilot/portable-reference`; the CLAP smoke also supports
`S3G_TRACKER_MAIN_CLAP_CAPTURE_DIR` for the embedded Console/Help pages.

The approved main/Warps/Song build was installed before starting this milestone,
with a recoverable backup. Console + Help have since been installed; see the
installation and keyboard follow-up above. Building alone never replaces the
installed bundle.
At the Console/Help milestone, Geometry, Bursts, Phrases, Assemble and Reshape
were still native. Geometry + Bursts have now been adapted; see the milestone above.

### Geometry + Bursts source-parity checklist

The original `S3GTrackerGeometryView` in `s3g_tracker_workspace.mm` remains the
independent reference. The audit was started before prioritizing the reported
Live Code keyboard regression and resumed after the user confirmed its fix.
The adaptation includes Pitch Map, not just the initial ring display and Burst
matrix. Retain the following contracts when extending these pages:

- All six Geometry views: Ring Field, Active Pulses, All Steps Underlay,
  Phase Spokes, Lane Focus and Composite Ring. Muted lanes keep their ring
  positions; Song playback follows the published sounding-pattern ID and row
  mute mask without changing the editor's selected pattern.
- The current family layout and suite panel/menu/slider metrics, 21-point menu
  rows, four-column scale menu, two-column Burst-slot menu, per-item separators,
  and centered button capitals. Preserve geometry's independent 65–180% ring
  zoom as distinct from 65–200% whole-editor scaling.
- Select/Paint/Erase/Velocity, Option erase, double-click bead reveal,
  lane/direction/morph menus, NOTE default, NOTE length with optional VOL link,
  reverse/reflect, and 25/50/75/100% morph. Stage length/default-note/rotation/
  density previews and publish a completed gesture only once.
- The visible R handle and ROTATE ROWS slider rotate authored rows; this is not
  the older documentation's phase/P handle. Density arcs, ghosts, velocity
  radial offsets, diagnostic paths and read-head markers must be traced from
  current drawing code, not approximated from the older documentation image.
- Burst bank/slot/name/UTF-8 byte limit, eight matrix rows and empty-row event
  creation, event removal/insertion, new/duplicate/delete with usage checks,
  even/accelerating/decelerating timing, reverse/rotate, pack import/export,
  copy-to-project, purge/delete-bank callbacks, Place and Fit Gates.
- Burst matrix field drags and Tab/Shift-Tab field selection, arrow-key editing,
  Shift coarse steps, ordered-neighbor bounds on onset edits, radial event
  positioning, velocity breakpoints, pitch trace, gate tails/spill indicators,
  and all three synchronized playback cursors.
- LISTEN, MIDI channel and LOOP audition retain existing coordinator callbacks.
  Loop audition is a separate row-duration timer; never emit MIDI from drawing
  or the GUI refresh callback. Hiding/closing/changing a bank or slot must stop
  the owned audition timer safely.
- Pitch Map's selected-row/full-cycle scope, root/scale, analyze evidence,
  low/high bounds, seven contours, leap/variation/anchor/transpose/invert/reverse,
  seed/preview/apply, polyphonic voicings and selected-point flags. Manual pitch
  drags snap to scale degrees; interval drags shift the following phrase.
  Generated previews freeze before manual editing. Apply changes explicit NOTE
  cells only, preserving chord voices and non-note cells.
- Playback halos use monotonic elapsed-time decay (140 ms half-life), and the
  overlays observe the existing published note-hit/row and subrow snapshot.
  No GUI-frame counter may drive musical time. Test the same elapsed interval
  at different refresh rates, including stalls, alongside the CLAP clock tests.

The old documentation's Shift velocity snapping and phase/P-handle wording was
corrected to describe the current native interaction: continuous radial velocity
and the R row-rotation handle. Song read-only and radial-center inconsistencies
are corrected explicitly in the portable version and covered by tests. Recall,
hide and cancel reject stale gestures. Phrases, Assemble and Reshape remain native.

## Portable Song milestone (2026-09-12)

`S3G_ENABLE_TRACKER_PORTABLE_SONG_PAGE_ON_MACOS=ON` enables Song and the
Warps/main/pilot prerequisites. All four options default OFF; the original
standalone and separately compiled Cocoa controller remain references.

The adaptation follows the current `s3g_song_window.mm`, the Song documentation
and `trackerSongFamilyLayout`, not the older screenshot's incomplete field set.
It retains:

- The original panels, table columns, measured 28-point header, 48-point cells
  with one-point spacing, grays, Menlo face and non-button glyph baselines.
- Add/inherit, duplicate, delete, up/down and gutter drag/Option-copy, stable row
  identities, single selection and all 32 per-row lane mutes. Lane reordering
  preserves the mute mapping without publishing a second coordinator edit.
- Pattern IDs/names, missing pattern and warp references, per-row saved warp
  selection, inclusive Loop In/Out, independent ticks/span, repetitions, tempo
  multiplier, energy and base/override swing. Saved values outside the ordinary
  menu choices remain visible and round-trip without silent replacement.
- Continuous swing dragging with a single publication on release, coarse and
  precise scrolling, Option/right-click base reset, and stale-gesture cancellation
  on restore, hide, Escape or mouse cancellation.
- Song mode, live loop, four queue boundaries, selected-row launch, and distinct
  current/pending row markers. Edits remain enabled while the host is playing.
- SONG FILE save/load callbacks for the complete arrangement and pattern bank.
  Native file dialogs remain a Mac service; the portable page does not own I/O.
- Item-separated canvas menus, keyboard menu navigation, full 256-row loop-menu
  access, horizontal/vertical scrolling and arrow/Home/End/Page navigation.
  A drag-only autoscroll timer reveals rows under a stationary edge pointer;
  it cannot advance playback or publish an arrangement before release.

`editor_song.h/.cpp` owns the UI-thread authoring model. `SongPageView` owns the
table and gestures, while `ToolPageView` provides reusable suite canvas controls
for subsequent pages. `s3g_tracker_song_page_host.*` only resolves Mac fonts,
focus, CFrame ownership and sizing. The existing Song controller forwards its
unchanged coordinator API to that page under the opt-in define.

The CLAP coordinator still owns project history, pending launches, deferred
runtime replacement at Song-row boundaries, MIDI/audio scheduling and the
timestamp-held display snapshot. No FPS-based Song clock or MIDI extrapolation
was introduced. Song remains embedded in CLAP, as before; this milestone does
not add a Song detach feature. Whole-interface resizing retains 65–200% scaling.

Intentional presentation differences are limited: portable overlay scrollbars
replace AppKit scrollers; the SONG FILE placeholder and row numbers remain
legible instead of reproducing native clipping. Following the user's Warps
review, button text centers on visible capital height, including Song buttons,
rather than on a font line box with unused descender space. This is not a claim
of full-page pixel identity or a complete cross-platform accessibility tree.

Validation uses `s3g_tracker_editor_song_tests`,
`s3g_tracker_portable_song_tests` and the extended `s3g_tracker_clap_smoke`:

- Independent Cocoa comparisons cover every menu title/selection (including
  missing/custom values), all eleven column geometries, all 32 mute buttons,
  row actions and field callbacks against VSTGUI frame events.
- GUI checks cover swing gesture publication/cancellation, precise scrolling,
  row move/copy, file and queue callbacks, long menus, narrow table access,
  drag autoscroll and display-refresh independence from playback state.
- Actual CLAP embedding checks native ADD at 65/100/150/200%, canvas-menu edits,
  Song mode, saved-state recall and editing/queueing during audio processing.
- All 36 targeted Tracker tests pass. As in the preceding milestones, this
  excludes the five known baseline assertions in `workspace_layout_appkit`;
  it is not a claim that the broader native workspace suite is green.
- Paired arrangement PDFs have 328 matched non-button table glyph boxes with
  no baseline difference above 0.01 point. Button baselines intentionally use
  the user-requested capital-height centering rule. Selected gray/background
  pixel samples also match the Cocoa reference.
- MinGW x86-64 accepts the Song model, tool controls, Song page and core tests
  with `_WIN32`. This is compile-only evidence, not a linked Windows CLAP.

Set `S3G_TRACKER_SONG_CAPTURE_DIR` for paired Cocoa/VSTGUI PDFs. Reference
captures are in `build-tracker-vstgui-pilot/portable-song`; actual CLAP captures
are in `portable-main-clap` beside it. Building does not install the new page.
The corrected Warps-only milestone was installed before beginning Song.
Song was installed on 2026-09-12 before beginning Console + Help, with the
previous bundle backed up. Restart REAPER to load that installed build.

## Portable Warps milestone (2026-09-12)

`S3G_ENABLE_TRACKER_PORTABLE_WARP_PAGE_ON_MACOS=ON` enables Warps and the
portable-main/pilot prerequisites. All three options still default OFF. The
original Cocoa Warps controller remains compiled as a separately selectable
reference, and as the reference in the parity test executable.

The port follows `s3g_tracker_warp_window.mm` and the existing
`trackerWarpFamilyLayout`, not the older documentation screenshot (which
predates automatic slot recall, the header SAVE action and playback bypass).
It retains:

- All 64 library slots, UTF-8 names, SAVE/Return, automatic occupied-slot
  recall, empty-slot selection and deletion without clearing the working stack.
- Serial EXP/STEP/EUCLID transforms, add/remove/clear and selected-transform
  controls, type defaults, mix, segment bounds, repetitions and cycle length.
- The original curve geometry, 256-sample composition, dashed identity,
  tick markers, bypass overlay, 192-sample playback progress and outlined cursor.
- Song's separate sounding stack/cycle and progress labels. Editing controls
  continue to address the working stack, just as in Cocoa.
- Suite panels, thin sliders, centered handles, item-separated canvas menus,
  disabled controls, conditional PULSES visibility, font faces and exact Mac
  glyph baselines. Generic VSTGUI text editing replaces NSTextField.
- Independent detached-window reflow, ordering and reattachment via the shared
  CLAP shell. Embedded Warps inherits the existing 65–200% outer magnification.

Two explicit refinements close existing usability gaps: double-click numeric
entry now works as already promised in the user documentation (the old slider
subclass only dragged); pop-outs below the 720 x 580 usable authoring canvas
scale proportionally so the shared 480 x 360 minimum cannot clip controls.
Numeric values remain legible rather than reproducing Cocoa decimal-readout
clipping. A text field's native CALayer border does not appear in Cocoa PDFs,
but is retained in the actual portable rendering. Full-page pixel identity and
a complete cross-platform accessibility control tree are not claimed.

Implementation boundaries:

- `editor_warp.h/.cpp`: platform-free authoring, validation, slot/transform
  descriptions and exact curve display-list generation.
- `s3g_tracker_warp_page.h/.cpp`: CFrame drawing, menus, pointer/key input,
  continuous sliders, generic text, cancellation and readout behavior.
- `s3g_tracker_warp_page_host.h/.mm`: Mac frame ownership, native focus,
  measured suite-font line boxes, resource runtime and independent resizing.
- The existing workspace coordinator still supplies the timestamp-held display
  snapshot. There is no new playback timer, MIDI scheduler or UI extrapolation.
  Hidden pages flush pending edit closure but do not request playback redraws.

Validation:

- Platform-free tests cover capacity, all slots, Unicode trimming, empty-slot
  and bypass semantics, type defaults/options, invalid atomic edits, and
  Song-vs-working curve selection, progress, cursor and identity dash.
- Windowed tests compare Cocoa selectors against actual VSTGUI input, verify
  numeric editing/Return/Escape, continuous dragging, canvas-menu navigation,
  64th-slot access, project recall, disabled controls and detached/reparented
  focus. Refresh stress at 15/30/60/120 calls per simulated second cannot
  advance the playback tick or publish transport changes.
- The actual CLAP smoke requires the portable Warps host when enabled, edits
  and serializes cycle values at 65/100/150/200% and before/after detaching,
  checks independent window ordering, switching to Tracker while detached,
  title-bar close and reattach.
- All 34 targeted Tracker tests pass; the CLAP and Warps window tests each pass
  three consecutive runs. This suite excludes `workspace_layout_appkit`, whose
  five pre-existing Cocoa assertions are documented below; it is not a claim
  that the broader native workspace suite is green.
- Paired captures cover empty, exponential, bypass, Euclidean/options, Song
  playback and detached states. The Song pair has 72 matched glyph boxes with
  no baseline differences above 0.01 point in the initial adaptation. Following
  user review, button titles intentionally use capital-height centering rather
  than Cocoa's line-box offset; other text retains its original baseline.
  Equal button-text padding is tested at 65/100/150/200%. The slot-menu capture separately
  records all-item separators and multi-column access.
- MinGW x86-64 accepts the Warps presenter, page, shared renderer and core test
  with `_WIN32`. This is compile-only evidence, not a linked Windows CLAP.

Build with the Warps option above and targets `s3g_tracker_clap`,
`s3g_tracker_clap_smoke`, `s3g_tracker_editor_warp_tests` and
`s3g_tracker_portable_warp_tests`. Set `S3G_TRACKER_WARP_CAPTURE_DIR` for paired
PDFs. Current artifacts are in `build-tracker-vstgui-pilot/portable-warps`; the
actual embedded capture is `portable-main-clap/portable-warps.pdf` beside it.
Building does not replace the installed plugin. REAPER review remains the next
user acceptance step.

## Portable main-page milestone (2026-09-12)

`S3G_ENABLE_TRACKER_PORTABLE_MAIN_PAGE_ON_MACOS=ON` enables the new page and
automatically enables the existing pilot/scaling support. Both switches still
default OFF, preserving a Cocoa reference and the standalone Mac app.

The new `MainPageView` is a real VSTGUI `CFrame` child. It includes:

- Pattern and View toolboxes, transport, tempo scale, swing, default gate,
  loop bounds, recording mode/lane and Live Code.
- Original compact/expanded grid, frozen row gutter, independent column
  read heads, mute overlays and editable volume/gate/sequence envelope.
- Cell/row selections, loop gestures, numeric dragging, inline note/chord,
  value, gate, SEQ/CC/condition entry, lane naming, length/stride and read start.
- Original keyboard commands, typed clipboard, structural row operations,
  selection transforms, Paste Special, statistics, and context-menu actions.
- Canvas dropdowns with item separators; links to the still-native Burst,
  Pitch Map and Phrase tools through service callbacks.
- Live Code completion, shared draft/history with the Console page, Return
  retaining command-entry focus, and Escape returning to the grid.
- Separate 55–180% grid zoom and existing 65–200% whole-interface scaling.

The source split is deliberate:

- `tracker/include/s3g/tracker/editor_grid*.h`, `editor_workspace_layout.h`
  and `tracker/src/editor/editor_grid*.cpp` contain ordinary C++ geometry,
  cell grammar, commands, selection/clipboard operations and display lists.
- `tracker/src/editor/s3g_tracker_main_page.*` and
  `s3g_tracker_main_menus.cpp` own VSTGUI events, scrolling, text editing and
  menus. Inline text uses VSTGUI's generic editor, not native `NSTextField`.
- `tracker/src/app/macos/s3g_tracker_main_page_host.*` only attaches the frame
  and supplies native focus, font metrics and clipboard revision services.
  The workspace adapter supplies shared console history and existing tools.

Native focus must be reconciled when entering the new frame from the mixed
Cocoa shell. A native first responder alone does not guarantee an active
`CFrame`; the adapter checks the actual responder before activating it.
The main page is pinned to exactly 1320 x 820 logical units: AppKit's
fractional-scale attachment rounding must not resize the VSTGUI canvas.
Generic text caret changes are queued after event dispatch to avoid the text
editor's recursive-key guard. Closing/hiding commits valid edits, cancels
invalid ones and releases momentary gestures without leaving dangling editors.

This is an adaptation of the original rendering, not a new visual design.
Mac font faces, baseline metrics and ColorSync conversion are retained.
Control outlines follow the **rendered** Cocoa reference: its `NSFrameRect`
uses the current fill color, despite a preceding `setStroke`. Introducing
contrasting borders here would visibly change the original interface.
The new page has portable overlay scrollbars; their visibility differs from
AppKit's system-managed autohiding scrollers. Full-page pixel identity is not
claimed. Tooltips and the native host accessibility label remain; a complete
cross-platform screen-reader control tree is a later editor-wide task.

### Graphics/MIDI timing

There is no new playback timer in the page. The CLAP audio callback still owns
MIDI scheduling and sample offsets. The existing coordinator applies its
timestamped presentation holdback before the page reads `TrackerViewState`.
UI refresh only invalidates affected playhead cells/envelopes; Song mute-mask
changes also invalidate the grid. It does not advance time, emit MIDI or
publish pattern edits. A slow or skipped frame can delay a picture, but must
not change musical timing. Real Windows/REAPER display latency and loaded-host
testing are still required when the Windows shell is implemented.

### Current validation

- Direct comparison against the original Cocoa responder: 364 cell-grammar,
  120 selection-operation and 120 keyboard-navigation cases. Original NOTE,
  VOL and SEQ context-menu leaf inventories remain reachable.
- Windowed VSTGUI tests exercise inline editing, chord preservation, header
  text selection, command history/completion, clipboard, loop selection,
  drag publication, gate output notification, Song read-only/mute refresh,
  zoom/scroll, invalid input and hide/reopen. Refresh stress at 15/30/60/120
  calls per simulated second leaves playheads/MIDI counters unchanged.
- A separate platform-free grid test checks transactional paste, full-precision
  typed clipboard versus externally replaced text, mismatched column rejection,
  qualified burst identity, structural rows and length/read-start grammar.
- The CLAP smoke exercises the actual native VSTGUI embedding and keyboard
  editing at 65%, 100%, 150% and 200%, all ten page buttons, close/reopen, and
  the existing sample-offset MIDI, gate, retrigger and rest-row CC assertions.
  Both GUI tests have also passed three consecutive runs; the CLAP matrix
  includes attachment at the smaller negotiated 900 x 586 initial size.
- The 30 core/editor/Song-roundtrip tests pass. The known broader AppKit
  workspace failures described below remain outside this milestone.
- MinGW x86-64 accepts all seven extracted controller/painter/main-page/menu
  translation units and the platform-free grid test with `_WIN32` enabled.
  This is a compile check, **not** a linked or runtime-tested Windows plugin.
- The five original grid/envelope zoom/scroll comparison captures also passed
  after the pure-C++ painter extraction, before replacing the main-page host.
  Those numeric pixel results apply to the renderer comparison, not to the
  entire new `CFrame` page.

To build and test this milestone:

```sh
cmake -S . -B build-clap-sample-vstgui-fidelity \
  -DS3G_BUILD_TRACKER_PREVIEW=ON \
  -DS3G_ENABLE_TRACKER_PORTABLE_MAIN_PAGE_ON_MACOS=ON
cmake --build build-clap-sample-vstgui-fidelity \
  --target s3g_tracker_clap s3g_tracker_clap_smoke \
  s3g_tracker_portable_main_tests s3g_tracker_editor_grid_tests -j 4
ctest --test-dir build-clap-sample-vstgui-fidelity --output-on-failure \
  -R '^s3g_tracker_(clap_smoke|portable_main_tests|editor_grid_tests)$'
```

GUI tests require a logged-in Mac window server. Set
`S3G_TRACKER_MAIN_CAPTURE_DIR` for the windowed test's compact/expanded/zoom PDFs,
or `S3G_TRACKER_MAIN_CLAP_CAPTURE_DIR` for the actual embedded main-page PDF.
Current artifacts live in `build-tracker-vstgui-pilot/portable-main-host` and
`build-tracker-vstgui-pilot/portable-main-clap`.

The compile-only Windows check, using the repository's pinned VSTGUI checkout:

```sh
x86_64-w64-mingw32-g++ -std=c++17 -D_WIN32 \
  -DVSTGUI_ENABLE_DEPRECATED_METHODS=0 -DVSTGUI_ENABLE_XML_PARSER=0 \
  -DVSTGUI_OPENGL_SUPPORT=0 -I tracker/include -I plugins/common \
  -I build-clap-windows-cross/_deps/vstgui-src -fsyntax-only \
  tracker/src/editor/editor_grid.cpp \
  tracker/src/editor/editor_grid_controller.cpp \
  tracker/src/editor/editor_grid_keys.cpp \
  tracker/src/editor/editor_grid_rows.cpp \
  tracker/src/editor/editor_grid_painter.cpp \
  tracker/src/editor/s3g_tracker_main_page.cpp \
  tracker/src/editor/s3g_tracker_main_menus.cpp \
  tracker/tests/editor_grid_tests.cpp
```

Building does not install the plugin. The built Mac bundle is
`build-clap-sample-vstgui-fidelity/plugins/clap_tracker/s3g_tracker.clap`.

## Earlier drawing pilot (reference and history)

The following describes the earlier pilot with the portable-main-page switch
OFF. Its native-input test/capture paths remain available for comparison.

- `TrackerViewState`, `WorkspaceCallbacks` and the audio-device value type now
  live in ordinary C++ headers under `tracker/include/s3g/tracker`.
- The Tracker grid, frozen row-number gutter, value/gate/sequence envelope and
  its playback overlay submit a portable display list to VSTGUI.
- The original draw routines still determine every cell, color, label,
  selection, mute overlay, read head and envelope point. This is an adaptation
  of those routines, not a replacement spreadsheet or a raster screenshot.
- A Mac adapter creates a VSTGUI drawing context for the current native view.
  The native views remain responsible for input, scrolling, inline editing,
  menus, accessibility and their existing undo/command callbacks.
- The existing toolbar, page strip, other nine pages, detached windows, dialogs
  and project storage are still Cocoa. No VSTGUI `CFrame` owns the workspace yet.

This intentional bridge separates drawing changes from application-scale input
changes. It is only used by the four audited draw paths. The standalone Mac app
and builds with the switch off continue using Cocoa drawing.

## Preserved contracts

- Plugin ID/version, MIDI ports/channels, scheduler/publication rules and
  `.s3gt` / `.s3gpack` formats are unchanged. No sampler/audio-device feature is
  introduced into the MIDI-only CLAP.
- The pilot's main CLAP window now scales the complete 1320 x 860 logical
  workspace proportionally from 65% (858 x 559) to 200% (2640 x 1720).
  Initial size fits the main display when necessary. Resizing enlarges/shrinks
  fonts, controls, graphics and canvas dropdowns together; it no longer adds
  logical workspace. The Cocoa-only reference retains responsive resizing.
- Grid magnification remains a separate 55–180% control with 100% reset.
- Detached tool windows keep their original independent responsive layouts;
  they do not inherit the main window's zoom until reattached.
- Night Tracker colors and exact resolved font faces are retained. IBM Plex
  Mono is bundled; the Cocoa code's existing monospaced system fallback for
  unresolved faces is preserved, not silently replaced with Helvetica.
- VSTGUI's Mac color-space conversion, stroked-rectangle semantics and exact
  face lookup are handled in the adapter. Native text baseline metrics are
  preserved, including fixed-height rows using AppKit's UI font fallback.
- The IBM font OFL is now packaged at `Contents/Resources/Fonts/OFL.txt`.

## Building and checking

Use an existing configured Mac CLAP/VSTGUI build, with Tracker enabled:

```sh
cmake -S . -B build-clap-sample-vstgui-fidelity \
  -DS3G_BUILD_TRACKER_PREVIEW=ON \
  -DS3G_ENABLE_TRACKER_VSTGUI_PILOT_ON_MACOS=ON
cmake --build build-clap-sample-vstgui-fidelity \
  --target s3g_tracker_clap s3g_tracker_clap_smoke -j 4
ctest --test-dir build-clap-sample-vstgui-fidelity \
  --output-on-failure -R '^s3g_tracker_clap_smoke$'
```

The switch defaults OFF. ON requires the existing portable GUI dependency;
the standard CMake helper supplies linking, resources and bundle signing.
Keep a separate OFF build as the reference, and never load both variants into
one test process (they contain the same Objective-C class names/plugin ID).

The smoke test's `S3G_TRACKER_EXPECT_VSTGUI=1` check requires all four drawing
surfaces to have used VSTGUI; a silent Cocoa fallback is a failure. CTest sets
this automatically for the pilot configuration. To capture a parity matrix,
also set `S3G_TRACKER_PILOT_CAPTURE_DIR` to a build-artifact directory. This
writes compact/expanded 100%, expanded 55%/180%, and scrolled 180% PDFs. The
existing `S3G_GUI_SMOKE_PDF_DIR` captures Tracker, Song, Geometry and Warps
with the documentation/playback fixture. Run each capture against both bundles.
Use `1320 860` (after the plugin ID) for paired captures: other outer sizes
intentionally differ from the old responsive editor now.

The smoke also checks whole-interface sizing, stable integer size negotiation,
both host resize orderings, font/control geometry and hit testing, canvas-menu
selection, and double-click inline editing with nested grid zoom at 55%, 100%
and 180%. Set `S3G_TRACKER_SCALE_CAPTURE_DIR` to write whole-interface PDFs at
65%, 100%, 150% and 200%.

The pilot uses a native magnifying viewport around the original logical
workspace. AppKit's scroll-view magnification preserves the Auto Layout tree
and field-editor/event coordinates; manually transforming the page's own
frame/bounds does not. `set_scale` correctly returns false for Cocoa, whose
logical points already account for Retina DPI; user zoom uses `set_size`.
Help explicitly uses TextKit 1 in the pilot. Automatic TextKit 2 viewport
layout could repeatedly invalidate the host window when opening Help at a
fractional outer scale (reproduced at 900 x 586), eventually raising an AppKit
layout-loop exception. The content, typography, selection and scrolling remain
native. The smoke checks the text engine and supports paired Help captures
through `S3G_TRACKER_HELP_CAPTURE_DIR`.

Compare the paired zoom captures with:

```sh
ruby scripts/compare-tracker-pilot-captures.rb COCOA_CAPTURE_DIR PILOT_CAPTURE_DIR
```

This checks every visible grid word's position, font-face inventory and the
whole-page raster difference (not just words present in both files). The PDF fixture makes
the existing on-screen viewport clips explicit, since AppKit's layer-backed
scroll-view clips otherwise leak into neighboring panels during PDF drawing.
Each PDF has a `.pdf.json` sidecar with the actual viewport rectangle; the text
checker uses it to exclude glyphs hidden by a PDF clipping path, since Poppler
still reports those invisible glyphs. Whole-page raster comparison is unmasked.

## Validation caveats

Verified on the development Mac, 2026-09-11:

- The scaling refinement passes the GUI/MIDI smoke at 858 x 559, 1320 x 860
  and 2640 x 1720. The seven-size resize matrix checks lower/upper clamping,
  non-proportional requests, repeated adjustment without pixel drift, both
  resize call orderings, and editing with all three tested inner grid zooms.
  The rebuilt Cocoa-only configuration also passes its native-size smoke.
- After the Help text-engine fix, 20 consecutive registered GUI/MIDI runs
  pass, including close/reopen and preserving non-default inner grid zoom.
  Paired Help PDFs contain the same 242 visible words and font faces; the
  largest text-coordinate difference is 1 point (a separator rule). Help is
  not included in the strict grid pixel-parity claim: the old TextKit 2 PDF
  has a white text background while TextKit 1 renders the configured dark
  background. Its paired captures were inspected separately.
- Both Cocoa and pilot CLAP GUI/MIDI smoke runs pass at 1320 x 860; the
  registered pilot smoke requests 900 x 620, negotiated to 900 x 586.
- 29 core/editor/Song-roundtrip tests pass, including the new drawing contract.
- All five paired zoom/scroll captures pass. Every visible grid word matches
  within 0.01 point, font inventories match, and normalized whole-page pixel
  RMSE is 0.00032–0.00489 (threshold 0.01). This is not a pixel-identical claim.
  This comparison was rerun at 100% outer scale after adding the magnifying
  viewport; results are in the `scaling-parity` artifact folders.
- Rebuilding with VSTGUI disabled preserves a pixel-identical Tracker capture
  against the pre-migration Cocoa bundle.
- Mac arm64 bundle signing verifies. The initial drawing pilot was installed
  before this scaling refinement. Building does not replace that installation.
  The current scaling build is
  `build-clap-sample-vstgui-fidelity/plugins/clap_tracker/s3g_tracker.clap`;
  `build-tracker-vstgui-pilot/s3g_tracker.clap` is the earlier drawing pilot.

The broader native `s3g_tracker_workspace_layout_appkit_tests` currently has
five failing assertions: Warps playback redraw, Reshape profile redraw, two
Geometry ring/mute expectations and Geometry view dispatch. All five reproduce
when linking the test with the **unmodified HEAD workspace source**; the pilot
does not enable VSTGUI in that native test target. They are not silently marked
passing or fixed as part of the drawing migration.

At 760 x 620 the CLAP smoke test's MIDI-statistics/HOST-BPM header-separation
assertion fails in the original responsive Cocoa editor. The scaling pilot
does not use that narrow logical layout: its full 1320 x 860 surface scales
down, preserving the header geometry at its 858 x 559 minimum.
The old Pattern Bank test's positional `SongRow` initializers were updated to
named field assignments so it builds with the already-existing row-ID field;
no production Song logic was changed.

## Remaining acceptance gates

1. Review the installed all-pages Mac build in REAPER. The primary page
   conversion is complete, subject to user acceptance and any parity fixes.
2. Execute the 40 core/filesystem/adapter tests and the linked HWND integration
   host on Windows. The new CI workflow is ready but has not run remotely.
3. Validate in Windows REAPER: project/pack round trips, detached windows, DPI
   and 65–200% resizing, text/Spacebar focus, MIDI scheduling, and cursor timing
   at different refresh rates and after GUI stalls. Preserve the existing
   audio-thread timing/mailbox contracts. Use `TRACKER_WINDOWS_README.txt`.
4. Address findings from that native-host run before adding Tracker to accepted
   Windows release packages. Consolidating the Mac native coordinator onto the
   extracted C++ coordinator can follow a separate Mac parity review; it is not
   required to run the Windows test build.

Tracker is a MIDI-only CLAP; the standalone Mac app is retired. Its native audio
devices, Audio Units and device services do not need to be ported or revived.
Shared legacy rack/sampler schema types remain for project compatibility.
No installed plugin is replaced merely by building; the explicit Mac
installation above is the current checkpoint.
