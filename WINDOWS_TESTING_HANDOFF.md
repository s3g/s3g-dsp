# Windows testing handoff — 14 September 2026

## Start here

This is a development handoff, not a certified Windows release. The test machine
is **Windows 10, Intel Core i7-4510U @ 2.00 GHz, 8 GB RAM**. Windows 11 has not
been tested by the user. Do not substitute Windows 11 results for this baseline.

1. Copy the handoff ZIP to Windows and extract it to a short local path, such as
   `C:\s3g`. Keep the ZIP unchanged as the transfer baseline.
2. Open the extracted `s3g-dsp-windows-handoff` folder. Read `START_HERE.md` and
   verify the payload before editing; `VERIFY_HANDOFF.ps1` checks its hashes.
3. Test the supplied Tracker binary before installing a compiler or rebuilding.
   The latest page-visibility correction still needs native Windows acceptance.
4. Install VS Code and the official OpenAI Codex extension, sign in with the same
   ChatGPT account, and open the extracted **`s3g-dsp` source folder**, not a Mac
   build folder. Paste `CODEX_START_PROMPT.txt` into a new Codex conversation.

Do not depend on this exact Mac conversation automatically appearing in the
Windows extension. This file and the prompt are the durable task context. The
handoff contains no account credentials, Codex session data, or editor settings.

The setup guidance follows the official [Codex IDE documentation](https://learn.chatgpt.com/docs/codex/ide).
Native Windows/PowerShell is supported; WSL is not required for this workflow.
OpenAI describes Windows 10 sandbox support as best effort (1809+ in practice),
so record the exact OS build if setup fails. Keep sandbox protections enabled;
run interactive REAPER checks from the normal Windows desktop. Sandboxed GUI
processes can use a private desktop and may not be visible to the user. See the
official [Windows sandbox guidance](https://learn.chatgpt.com/docs/windows/windows-sandbox).

## Source and build provenance

The source baseline is branch `main`, commit
`892e0af704b5316fce6b1c388f96f6c49ed2389b` (`tracker updates`). The working tree
was clean before adding this handoff and its packaging files. See the generated
`SOURCE_SNAPSHOT.json` for the actual commit, local changes, file modes and SHA-256
hashes at packaging time; the new handoff files are not falsely attributed to
that commit.

The source snapshot includes all regular tracked files plus the explicitly
listed handoff files. It deliberately excludes Git history (`.git`), ignored
build/cache/output folders, personal configuration and sibling repositories.
The tracked `max` symlink points outside this repository to `../s3g-clap-max`;
it is **omitted, not followed**, and recorded in the snapshot manifest. It is
not needed to build the CLAP plug-ins. There are no Git submodules at this baseline.

External CLAP/VSTGUI/WORLD dependencies are not vendored in the handoff. A fresh
build needs network access to fetch the versions pinned by CMake. Existing Mac
CMake caches and cross-build directories must not be reused on Windows.

| Supplied Windows test ZIP | SHA-256 |
| --- | --- |
| `s3g-dsp-windows-clap-suite-x64-test-20260914-130216.zip` | `f1094d38ac3686bcf7bce5854b1d0e48cf9040bf9a6728ff5ad1d0ee25770a0d` |
| `s3g-tracker-windows-x64-test-20260914-130343.zip` | `9c59539ed982a55f28ce60029a84e2736fefabc5cf99a06b4df2d7ba72aca5e7` |

The full suite contains **121 canonical CLAPs; only Ambi Energy is excluded**.
It includes shared fonts/licenses, Imprint/Ray response atlases, and Tracker's
integration checker. Ambi Energy's separate renderer probe is not a CLAP port.
The Tracker-only ZIP is an alternative for focused testing: do not install a
second Tracker alongside the suite. Their Tracker PE binaries differ in strip
timestamps/checksums; this is not a different source fix.

Both packages were cross-built on Mac. Successful compilation, archive checks
and Mac regressions do not establish native Windows or REAPER acceptance.
The older suite ending in **`095152.zip` predates the page fix**; do not use it
as the current baseline. No new DSP/GUI code was changed for this handoff.

## First task: verify Tracker embedded pages

Reported on Windows: page buttons left the editor blank. Floating a detachable
page and closing it made that page visible after reattachment. Tracker and Song
cannot detach, so that workaround could not expose them.

The corrected Windows host raises the selected embedded page above the shell
using sibling `SetWindowPos(HWND_TOP)` and `WS_CLIPSIBLINGS`. It does not make
windows globally topmost or change MIDI/DSP processing. The integration checker
now tests actual page exposure, including an intentionally occluded positive
control, rather than checking visibility flags alone.

1. Extract the Tracker-only ZIP to its own folder. In ordinary interactive
   PowerShell, change to the folder containing the `.clap` and `.exe` and run:

   ```powershell
   & .\s3g_tracker_windows_clap_smoke.exe .\s3g_tracker.clap 2>&1 | Tee-Object -FilePath tracker-smoke.txt
   $trackerSmokeExit = $LASTEXITCODE
   Write-Host "Tracker checker exit code: $trackerSmokeExit"
   ```

   Alternatively double-click `run-windows-check.cmd`. This opens temporary
   test windows but does not install anything or launch REAPER. Keep the log
   and exit code. A pass is not a substitute for the host checks below.
2. Quit REAPER before replacing any loaded binary. Back up the old test folder,
   then use one package in one configured CLAP search path; avoid duplicate
   Tracker copies. Keep the sibling `Resources` folder and license files with
   the binary. Fonts do not need a system installation.
3. On first open in REAPER, verify **TRACKER is visible immediately**. Single-click
   SONG and all other tabs **before detaching any page**. Confirm actual drawing
   and mouse input, not just a selected tab label. Then test float/reattach.
4. Recheck at 65%, 100%, 150%, 200% outer-window sizes, hide/show, project recall
   and two instances. Record Windows display scaling/DPI and REAPER version.
5. If it still fails, capture the first failing page, screenshot, checker log,
   exact plug-in path/hash, display scaling and reproduction steps before edits.

Relevant code and detailed acceptance checklist:

- `plugins/clap_tracker/s3g_tracker_windows_editor.cpp`
- `tests/tracker_windows_clap_smoke.cpp`
- `plugins/common/TRACKER_MAC_VSTGUI_MIGRATION.md` (latest correction first)
- `plugins/common/TRACKER_WINDOWS_README.txt`
- `docs/s3g-tracker.html` and `docs/building-from-source.html#tracker-builds`

All ten pages must remain intact: Tracker, Song, Geometry, Bursts, Phrases,
Assemble, Reshape, Warps, Console and Help. Tracker is CLAP-only; the standalone
Mac app was retired. It generates MIDI and needs an instrument for audible tests.

## Second task: measure Point / Pyrosphere performance

This remains **unresolved**, not a claim that 8 GB RAM is insufficient. On this
machine the user reports slow editor loading, sluggish graphics and audio
dropouts. Reducing point/voice counts and ambisonic order helps. Separate GUI,
real-time DSP and memory pressure before recommending minimum requirements.

Record exact Windows build, REAPER version, audio interface and driver, sample
rate, buffer size, display resolution/scaling, GPU/driver, active plug-in count,
ambisonic order and point/voice count. Save a small reproducible REAPER project
and preset; note any decoder/routing in the chain. Do not include private audio
or unrelated projects in reports without permission.

For each encoder separately, use the same project and change one factor at a time:

- Compare editor closed versus open, without bypassing the DSP. Record first-open
  and repeat-open times, REAPER real-time CPU/longest block when available, and
  audible dropouts. Measure frame times/FPS if instrumentation is added; otherwise
  label responsiveness observations as subjective.
- At a supported fixed sample rate (48 kHz if available), compare 128/256/512-sample
  buffers. Record actual driver settings rather than assuming they took effect.
- Compare lower versus higher order with point counts held constant, then fewer
  versus more points/voices with order held constant. Restore the baseline between
  comparisons. Third order has 16 channels; seventh order has 64, but this alone
  does not predict whole-plug-in CPU cost.
- Record process memory, system available RAM and paging/hard-fault activity with
  Task Manager/Resource Monitor alongside CPU. Close or idle VS Code, compilers
  and other heavy tools for final acceptance runs on the 8 GB machine.
- Keep host-scheduled audio/MIDI timing independent of GUI refresh. Do not fix
  graphics load by changing the musical clock or frame-based scheduling.

Inspection leads, not established sole causes: Point's editor invalidates on a
33 ms timer (`plugins/common/s3g_ambi_point_encoder_vstgui.cpp`); the shared canvas
also uses periodic redraw (`plugins/common/s3g_vstgui_canvas.h`). Point/voice count
and output order also increase DSP work. The current `clap_realtime_audit.cpp`
uses POSIX loading and is not a ready-made native Windows benchmark. Do not claim
Windows p99/RAM/FPS measurements from Mac tests or a cross-compile.

Publish minimum **tested workload profiles**, with buffer/sample rate/instance
count and headroom, only after measurements. No universal 16/32 GB minimum or
performance fix has been established.

## Native Windows build, only after prebuilt baseline testing

VS Code/Codex is not itself a C++ toolchain. For the following configuration use
Visual Studio 2022 Build Tools with Desktop development with C++, an appropriate
Windows SDK, Git and CMake. Run from the source root in a development environment
where those tools are available. These commands follow the repository's native
Windows Tracker workflow; `--parallel 2` limits concurrent build memory on this
machine (use 1 if necessary).

```powershell
cmake -S . -B build-tracker-windows -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DS3G_BUILD_CLAP_PLUGIN=ON -DS3G_BUILD_TRACKER_PREVIEW=ON -DS3G_ENABLE_PORTABLE_CLAP_GUI=ON -DS3G_BUILD_STANDALONE_APPS=OFF -DS3G_BUILD_FUTURE_COMPONENTS=OFF -DS3G_ENABLE_WORLD=OFF
cmake --build build-tracker-windows --config Release --parallel 2 --target s3g_tracker_windows_clap_smoke s3g_tracker_portable_core_tests s3g_tracker_workspace_layout_tests s3g_tracker_grid_selection_tests
ctest --test-dir build-tracker-windows -C Release -R "^s3g_tracker_" --output-on-failure
```

Check each command's exit code and stop on failure before continuing. Do not
reuse that build directory with a different compiler/generator. Dependencies
are fetched during configure. CLAP is pinned to 1.2.6; use CMake's existing pins.

Native Release output is in `build-tracker-windows/plugins/clap_tracker/Release/`.
Keep its plug-in, checker, Resources and licenses together. The additional CLAP
MIDI-adapter test requires `S3G_TRACKER_CLAP_TEST_INCLUDE_DIR` as documented in
`.github/workflows/tracker-windows-clap.yml`. The workflow existing in the repo
does not mean it ran or passed. Its `windows-latest` runner is not this Windows
10 acceptance machine. The existing `clap-windows-pilot` build preset targets
an older subset; Mac cross-build packaging scripts are not native Visual Studio
packagers. WORLD is disabled here for focused Tracker testing, not for a full
suite build that needs it.

## Preserve the established GUI and musical behavior

The original Cocoa interfaces and the family docs are the references, not newly
invented approximations. Preserve waveform/scope/cursor visualizations, custom
menu separators and compact submenus, centered button text, slider alignment,
presets, file dialogs, parameter gestures, lane colors and project formats.
Use lower-case `s3g` and upper-case plug-in titles. Keep bundled fonts and safe
fallbacks: Fira Code for shared controls, IBM Plex Mono for the Tracker grid.
CoreText and DirectWrite antialiasing need not be pixel-identical.

Preserve proportional **65–200% outer-window resizing**; Tracker VIEW grid zoom
is a separate **55–180%** control. Preserve detached-page layouts, Windows Ctrl
editing shortcuts, and Live Code spaces staying in the text field rather than
triggering REAPER transport. Phrase/Assemble LISTEN loops must follow host BPM
without a restart pause; GUI FPS must not control MIDI scheduling.

Recent VIEW Live Code commands, already implemented and documented:

```text
view status
view follow static|center|page
view source selected|lane|@alias
view resume
view zoom 55..180|+|-|reset
view notes name|midi
view jump 1..16
view detail on|off
```

Choose one alternative, not the literal pipes; `lane` means a lane number, e.g.
`view source 1`. Mode/source are saved preferences. Zoom/detail/manual hold are
editor state; these commands must not republish musical runtime or change MIDI.
Do not broaden this handoff into new migrations or an Ambi Energy port.

## Working safely and returning changes

This source snapshot is usable without Git but contains no `.git` history. Before
changing source, either use an authenticated clone containing the recorded base
commit and carefully compare/overlay the snapshot, or initialize a **new local
baseline repository** inside the snapshot. Do not overwrite another working tree.
For a standalone baseline, after verifying the transfer, optional commands are:

```powershell
git init
git config core.autocrlf false
git add .
git commit -m "Windows handoff snapshot from 892e0af"
```

These create local history only; do not copy this `.git` back over the Mac repo.
Configure a local Git identity if needed; do not change global settings just for
the handoff. Keep an untouched extracted snapshot or ZIP for comparison. Do not
publish/push, install over loaded plug-ins, remove old packages, or disable
security protections without the user's direction. After approved edits, return
a reviewed patch or changed source files with paths, baseline commit, exact build
commands, test results and binary hashes. Apply changes to the Mac repo only after
checking its current state; avoid two machines editing the same files blindly.

For the next report include: date; OS/REAPER/driver versions; package SHA; issue;
minimal reproduction; expected/actual behavior; checker exit/log; screenshots;
audio settings and CPU/memory measurements; any source changes and tests. Codex
can inspect source and logs and run available tools, but real REAPER interaction
and listening remain manual acceptance unless explicit tooling is available.
