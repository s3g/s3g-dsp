# Max design reference

The Max project is a behavior and interaction reference, not a file-format
compatibility target. `s3g Tracker` will not import its JSON, preserve saved
playheads, reproduce accidental phase quirks, or repair the old corpus.

## Audited source

The 2026-08-05 architecture pass audited:

```text
/Users/s3g/Documents/Max_s3g/v8_tracker/polytracker_decoded_output/
```

| File | SHA-256 |
| --- | --- |
| `polytracker_decoded_output.maxpat` | `ddb05c17beefcaf296fb905b8598bac826d79ab21c25f91203c08f77610c614d` |
| `tracker_v8ui.js` | `85aab0cc12e02aa32ac2b8b1f08a15dd1372344293663b6ae4ce31a49bb5fc98` |
| `form_v8ui.js` | `ca57b926f1b8c65cc2b5691864416f576c574bf2d9cedb6503f26b2aad96f595` |
| `live_console_v8ui.js` | `8b31b73e5ae57db0e3a4d9998b0820bb8d4f699b300a70d71b085126793d98c3` |
| `transform_v8ui.js` | `794edb568474c239244ee411d53f0f0356447b2669e269eb62680aaa5ac34ce1` |
| `envelope_v8ui.js` | `166ca41290f94a88f9d02d5cc6d0d6542dacece889c1f0d902e629643a9470ba` |
| `LIVE_CODING_COMMANDS.txt` | `b6d8878d245df8c4d0f363672ae305208d02f80759546d5ed409417ebdaf087e` |
| `polytracker-song.json` | `5f031fe58851a14c5a13a97043f5dc3572e89c8d49c419d0d0adc289b1b1eaa0` |

The `.maxpat` is Max 9.1.4 with 70 top-level boxes and 84 patch cords. It is
mostly orchestration. Six relative v8ui scripts contain the application; the
main tracker script alone is 4,872 lines.

## Behavior inventory

- Seven independent columns per track: `NOTE`, `INS`, `VOL`, `FX1`, `V1`,
  `FX2`, `V2`.
- Per-column length, playhead, stride called `speed`, mute, and
  forward/reverse/random/palindrome direction.
- Decimal/normalized string cells plus rest `---`, empty `..`, random `??`,
  force `!!`, mute `--`, and inherit `==`.
- Snapshots, quantized recall, chains, morph/vary, seeded and unseeded
  generation, Euclidean/mask/sieve tools, drum/bass scenes, roots/scales.
- Song rows with pattern, repeat count, eight-bit NOTE mute mask, optional BPM,
  row ticks, and swing; queue/next/previous/loop/follow.
- Command console, aliases, history/completion, envelope editor, transform
  panel, Euclidean geometry, mouse/keyboard editing, and local clipboard.

The Max clock is `metro 16n`, four ticks per beat. Every tick advances all
seven columns first, then decodes one composite event per track. MIDI leaves as
pitch/velocity/channel through a fixed 160 ms `makenote` gate. Microtiming and
repeat FX use Max JS `Task` scheduling with a fixed lookahead, not sample-time
events.

## Semantics to reconsider, not inherit blindly

- Cold playback currently advances from serialized playhead 0 to row 1 before
  the first decode.
- Pattern files serialize runtime playheads, ping-pong direction, and mute
  along with content.
- A NOTE rest is silent but leaves earlier note memory available to a later
  `==`; `--` persists a muted state.
- Empty FX `..` is silent without erasing remembered FX; `==` can resume it.
- If the same internal FX appears in both FX lanes, FX2's amount wins.
- Song repeats continue phase; they do not relaunch a pattern each time.
- The existing Euclidean distributor is a floor-difference algorithm, not a
  promise of canonical Bjorklund output.
- Swing delays alternate note output; it does not alter the Max metro itself.

The native engine makes one implemented policy explicit: a new pattern starts
at row 0 and emits before advancing. Rest, retrigger previous, future tie/gate
extension, note kill, column mute, song continuity, and the FX vocabulary are
specified on their own terms rather than inherited from Max symbols.

The first additional transform audit now has native, transactional commands
and exact unit fixtures for the useful mask semantics: `sieve`, `density`,
`thin`, `rotatehits`, and `humanize`. The three probabilistic commands consume
a session-owned deterministic stream only after validation succeeds. This
ports behavior, not JavaScript structure or Max task scheduling; `humanize`
still moves a hit by a neighboring tracker row and is not yet sample-domain
microtiming.

The first whole-pattern pass now exposes native `generate`, `generateseed`,
named `scene`, scoped `mutate`, and seeded `drumscene` commands. Generation
authors NOTE, INS, VOL, FX1/V1, and FX2/V2 plus independent length, stride,
phase, and direction. Seeded commands use a stable local stream and therefore
do not perturb later unseeded edits. Drum scenes retain the Max role recipes
for `techno`, `broken`, `sparse`, `blast`, and `ritual` while writing typed
native NOTE cells and configured kit pitches.

This is a semantic port, not byte-identical Max output. The native model does
not yet store per-lane root/scale metadata and has no exact typed equivalents
for Max `??`, `!!`, or per-cell value mute. The `symbols` control therefore
uses supported rest, previous, kill, empty, and default states, and melodic
generation varies around the lane's stable note anchor. Generated FX are drawn
from the native destination-neutral sequencing-action catalog; arbitrary Max
FX tokens are never inserted into typed cells.

Bank-level `variation`/`vary` wraps those generators transactionally: it clones
the active entry's routing, aliases, and anchors into the next stable pattern
ID, applies one generation or mutation command to the clone, and preserves the
source. Optional `launch tick|beat|cycle` publication prebuilds the replacement
runtime on the control thread and defers the audio-thread handoff until the
requested logical boundary. Song playback remains authoritative when enabled,
so it refuses a competing bank launch.

## Why the old corpus stays out of scope

An explicit `jq empty` pass over top-level, `patterns/`, and `songs/` JSON found
87 files total and **23 invalid files**. Failures include
`polytracker-song.json`, `polytracker-recovery.json`, and 21 pattern files.
Several contain trailing duplicated fragments or unmatched braces.

This reinforces the clean-break decision. Do not copy, repair, or import this
corpus as part of application development. Create small native fixtures that
express the desired rhythms directly in the new schema.

## Reimplementation order

1. Establish the native workspace as a coherent instrument: editable tracker,
   Rhythm Geometry, Volume Envelope, live console, and alternate Song window.
2. Prove timestamped CoreMIDI output with independent column phase and explicit
   retrigger/rest/Kill behavior.
3. Extend typed note/articulation and value cells with independent timing and
   FX fields while keeping definitions separate from playback state.
4. Connect song form, richer envelopes/transforms, and high-value console
   commands through shared semantic actions with undo.
5. Define the new strict project schema, quantized launch behavior, native test
   fixtures, and deterministic session rules.
6. Add controller bindings and feedback only after those semantic actions are
   stable; there is no Max parser or file-format translation layer.

The breakbeat slicer, wave terrain, and graphic waveshaper patches in the same
folder are separate experiments, not dependencies of the main tracker patch.
