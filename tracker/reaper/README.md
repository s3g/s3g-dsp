# s3g Tracker real-REAPER acceptance harness

Contract version 1 runs inside REAPER itself; it complements the mock-host
CLAP smoke test. The harness creates a new project tab, inserts the installed
Tracker and the MIDI-capture JSFX, then checks actual host transport playback,
sample-offset MIDI delivery, seek/restart, loop discontinuities, project save
and reopen, and playback after reopen.

Install the harness on Mac (the installer and `/tmp` paths below are Mac-focused):

```sh
./scripts/install-tracker-reaper-acceptance.sh
```

In REAPER, open Actions, load/run
`Scripts/s3g/s3g_tracker_acceptance.lua`, and leave the generated project tab
open until the result dialog appears. The detailed log and generated v1
acceptance project are written to:

```text
/tmp/s3g-tracker-reaper-acceptance.log
/tmp/s3g-tracker-reaper-acceptance-v1.rpp
```

The script never changes an existing project tab. The generated acceptance
tab and `/tmp` artifacts may be discarded after inspection.

The current user workflows are documented in
[`docs/s3g-tracker.html`](../../docs/s3g-tracker.html). Tracker is CLAP-only;
there is no standalone application acceptance target. The following editor-owned
interactions extend the automated host pass. Record the platform, REAPER version,
plug-in build/hash, sample rate, buffer size, and display scale with the results:

1. Open Tracker, choose the recording `LANE`, select `REC STEP`, hold a three-note
   chord with visibly different velocities, and verify one paired NOTE/VOL
   stack is written to the armed lane. The row should advance by View JUMP
   only when the final key is released; editing-lane selection stays
   independent.
2. During pattern playback choose `REC Q`, play across several ticks, and
   verify notes on the same row form a paired chord, notes wrap inside the
   armed NOTE lane length, and the cursor follows each written row without
   auto-advancing.
3. During pattern playback choose `REC MT`, play slightly ahead of
   and behind ticks, and verify MT appears in an available SEQ pair.
4. Type multi-word Live Code on both Tracker and Console, including detached
   Console. Space must insert text without starting REAPER transport. Check
   selection, copy/paste, history, completion, Return, Escape, and click-away
   focus. Grid copy/paste uses Control on Mac and Ctrl on Windows; focused text
   fields use Command on Mac and Ctrl on Windows. Test an exact-value field too.
5. Queue Song rows at `NEXT TICK`, `NEXT BEAT`, `END OF PASS`, and `END OF ROW`
   using `SELECT QUEUE`. Verify the amber pending marker changes at the named
   boundary and that pattern, warp, BPM ratio, swing, energy, and mutes switch
   together. Include row loop ranges, repeats, and the final non-looping row.
6. Resize the embedded interface through 65–200%, then vary Tracker's independent
   grid zoom through 55–180%. Detach and reattach Geometry, Bursts, Phrases,
   Assemble, Reshape, Warps, Console, and Help; closing a tool must reattach it.
   Check compact submenus, text alignment, scrolling, and ownership in front of
   the host window. Keep a second Tracker instance open to check isolation.
7. Author a long polymetric pattern. Confirm STATIC preserves the default view;
   CENTER tracks the chosen NOTE lane and PAGE advances in aligned blocks of up
   to 16 rows (fewer at larger grid zoom). Select a different editing lane while
   pinned, then test the selection-following source. Test reverse, palindrome,
   random, unequal lengths, lane reorder/removal, pattern changes, and Song
   changes. Manual navigation or inline editing must hold the view until RESUME;
   no mode may alter recorded MIDI timing or the armed recording lane.
8. With host transport stopped, audition Phrases and both Assemble preview scopes
   through a receiving instrument. Loop material with a note at row 1 and an
   intentional trailing rest. Measure consecutive row-1 MIDI starts against the
   full authored duration at the observed host BPM, not the final note-off or
   GUI refresh. Repeat at different tempos and buffer sizes. Stop LISTEN, hide
   the page, or start the host; preview notes must be released with no extra loop.
9. Check Geometry edits and undo; Burst placement/shared-definition changes;
   Phrase capture/replace/merge; Assemble reorder/duplicate/fit; Reshape preview
   versus committed variants; and named Warps recalled from Song. Confirm their
   cursors follow audible events even after a slow GUI refresh, without a MIDI
   timing change. Do not use display FPS alone as a synchronization measurement.
10. Save/reopen the REAPER project and separately save/load `.s3gt`. Check pattern
    and Song data, MIDI channels, banks, assembly staging, and follow settings.
    Import/export `.s3gpack` through a path with spaces and non-ASCII characters;
    imported assets must survive reopening without the original pack file.
11. Trigger PANIC and confirm the capture/receiving instrument retains no notes.

Windows acceptance remains pending. Use the Windows package's README and
`run-windows-check.cmd` for the native integration check, then perform the manual
host checks above on Windows. Also move between monitors with different DPI,
check file-dialog filters and clipboard Unicode, and test missing-font fallback
with an isolated copy. Do not treat Mac results or cross-compilation as Windows
host acceptance. The Mac harness installer is not a Windows installer.

MIDI-file import is intentionally outside the current-version contract.

## VSTGUI documentation screenshots

The September 2026 website refresh uses actual Mac VSTGUI captures, not Cocoa
reference renders or Windows acceptance evidence. With the full portable-shell
Mac option enabled as described in `docs/building-from-source.html`, build and
run the focused capture scenarios in a logged-in graphical session:

```sh
cmake --build build-tracker --parallel 4 --target s3g_tracker_clap_smoke \
  s3g_tracker_portable_geometry_tests s3g_tracker_portable_authoring_tests
tracker_captures="$(mktemp -d /tmp/s3g-tracker-docs.XXXXXX)"
env S3G_TRACKER_MAIN_CLAP_CAPTURE_DIR="$tracker_captures/clap" \
  S3G_TRACKER_GEOMETRY_CAPTURE_DIR="$tracker_captures/geometry" \
  S3G_TRACKER_AUTHORING_CAPTURE_DIR="$tracker_captures/authoring" \
  ctest --test-dir build-tracker \
  -R '^s3g_tracker_(clap_smoke|portable_geometry_tests|portable_authoring_tests)$' \
  --output-on-failure -j1
```

Use the following PDF sources for the website stems. The tool-page fixtures also
emit `-cocoa` comparisons; those are not the current VSTGUI documentation images.

| Capture below the temporary directory | Website stem |
| --- | --- |
| `clap/org.s3g.s3g-dsp.tracker.portable-main.pdf` | `s3g-tracker` |
| `clap/org.s3g.s3g-dsp.tracker.portable-song.pdf` | `s3g-tracker.song` |
| `clap/org.s3g.s3g-dsp.tracker.portable-warps.pdf` | `s3g-tracker.warps` |
| `geometry/geometry-0-vstgui.pdf` | `s3g-tracker.geometry` |
| `geometry/bursts-vstgui.pdf` | `s3g-tracker.bursts` |
| `authoring/phrases-vstgui.pdf` | `s3g-tracker.phrases` |
| `authoring/assemble-vstgui.pdf` | `s3g-tracker.assemble` |
| `authoring/reshape-vstgui.pdf` | `s3g-tracker.reshape` |

Inspect each capture before publishing. Render with
`pdftoppm -png -singlefile -r 216 SOURCE.pdf docs/assets/plugin-guis/STEM`,
and retain its PDF as `docs/assets/plugin-guis/masters/STEM.pdf`. PNGs are tracked;
PDF masters are local generated assets ignored by repository policy. Refresh the
page's screenshot cache suffixes and accurate captions, then run
`python3 scripts/check-docs.py`. These portable capture names are distinct from
the generic `S3G_GUI_SMOKE_PDF_DIR` pipeline; do not substitute its older native
Tracker captures for the current pages.
