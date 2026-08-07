# Live internal audio

The optional internal-audio build runs a hybrid indexed rack inside the
standalone app. It prepares five independent instance nodes for the `s3g-dsp`
membrane-kick CLAP while exposing only instances added to the song index,
prepares five DaisySP drum families and three native stereo samplers, enumerates
selectable stereo Core Audio outputs, and derives internal audio or timestamped
MIDI from one canonical scheduler stream. CLAP remains a reusable instrument
boundary; it is not the tracker model, editor model, or master clock.

## Runtime path

```text
AUHAL callback publishes block device-frame + host-time snapshot
                              │
                              ▼
sole Sequencer worker schedules the callback-derived horizons
              │ one ordered ScheduledEvent slice
              ├─ MIDI OUT ──────► bounded staging ─► CoreMIDI timestamps
              └─ internal inst ─► fixed-batch SPSC audio queue
                                         │
                                         ▼
                                 AUHAL callback offsets
                                         │
                                         ▼
 TrackerAudioEngine / five kick CLAPs + DaisySP drums + stereo samplers
                                         │
                                         ▼
                       summed 16ch ACN/SN3D -> stereo
```

The worker, not the callback, owns `Sequencer`. This keeps live whole-pattern
replacement allocation away from the render thread while still making Core
Audio the only timing authority. Every audio batch is published transactionally
so a same-sample `NoteOff/Parameter/NoteOn` group cannot be split by queue
pressure. Epoch changes discard stale batches on stop, restart, or fault.

The audio queue covers `max(20 ms, two callback budgets + 12 ms)`. Events for
paired MIDI are retained in a bounded worker-side queue and submitted only to
`max(20 ms, one callback budget + 12 ms)`. The callback budget is derived from
the opened device's reported maximum and its fixed/variable-buffer settings;
it is not a hard-coded frame count. Start places the first event beyond
`max(32 ms, one callback budget + 12 ms)`. The non-audio build separately uses
the fixed 20 ms MIDI-only horizon.

The callback performs prepared, bounded work only: apply a pending voice reset,
consume due events, translate absolute device frames to block offsets, render,
and scatter the two planar channels into the device's `AudioBufferList`. It
handles interleaved and non-interleaved buffer groupings and clears unused
hardware channels. The new timed callback in the shared `s3g-dsp` AUHAL wrapper
delivers raw host time, sample time, and their validity flags before rendering.
The tracker validates both sequences before it publishes the clock or renders;
after-callback telemetry is diagnostic corroboration, not timestamp preflight.

## Instrument and FX boundary

- `ScheduledEvent` remains destination-neutral and addresses a graph node as
  well as a parameter.
- The prepared graph uses stable physical node IDs that are independent of the
  decimal order shown in the song instrument toolbox:

  | Node | Instrument | Console name |
  | ---: | --- | --- |
  | 0..4 | capacity for independent `MEMBRANE KICK` instances | `kick` for the initial instance |
  | 5..12 | reserved, inactive IDs from the archived chip experiment | unavailable |
  | 13..27 | three instances of each of five DaisySP drum voices | assigned from the toolbox |
  | 28..30 | native `STEREO SLICE SAMPLER` instances | `sampler` / `sample` / `slice` for the initial instance |
  | 31..38 | independent CoreMIDI devices | `midi` for the initial instance |

  The new-song index maps `00 -> node 0`, `01 -> node 28`, and `02 -> node 31`.
  `+ ADD` claims another free node for the selected active instrument type.
  The console names address initial instances; the toolbox assigns added ones.
  `instrument <lane|@alias> <name|index>` sets the lane default and the row
  form writes an optional
  explicit `INS` cell. Instrument cells can change memory on rests, and every
  note carries its resolved concrete node in the canonical stream. A NoteOff
  still returns to the node that received its onset.
- `InstrumentNode` accepts fixed-capacity note/parameter events and prepared
  planar buffers.
- Each `MembraneClapNode` owns one instance's voice, parameters, and serialized
  state. Parameter metadata is cached during creation; normalized values
  become native CLAP values without render-time discovery. The canonical
  event type can represent Global, Channel, and Note scopes, but each current
  membrane instance has one global parameter state and its catalog therefore
  advertises Global only.
- Five DaisySP drum models each have three prepared, state-isolated instances.
  Their bounded musical controls use the same frame-zero mailbox contract as
  the membrane editor.
- Each `StereoSliceSamplerNode` has 32 fixed voices, immutable decoded sample
  storage, exact-offset onsets/releases, per-slice reverse and gain, and
  note-to-slice mapping from an editable base note. Three independent sampler
  instances feed a stereo bus after the ambisonic instrument decode and before
  MAIN OUT.
- The five membrane nodes share one `EmbeddedClapEntrySession`. The compiled-in entry is
  initialized once, all instances are destroyed before the session is
  deinitialized, and no node outlives the shared entry. This is entry-lifetime
  sharing only; plugin state and audio tails remain independent.
- The initial action catalog intentionally omits Trigger and the topology-
  changing Output Format parameter. Stable keys such as `membrane.tune` and
  `membrane.click` are the authoring contract; numeric CLAP IDs remain an
  internal compiled representation.
- Catalog actions are lane-relative. `kTrackInstrumentNode` resolves to the
  lane's remembered per-row instrument before duplicate suppression, so an FX
  pattern follows `INS` without embedding a rack number in every FX cell.
  Explicit note releases retain their onset node. Note-scoped actions on a
  rest likewise retain the active note's node rather than following unrelated
  instrument-memory changes.
- `TrackerAudioEngine` consumes events resolved to internal rack instruments
  and decodes fixed 16-channel ACN/SN3D output. The graph visits the complete
  five-node kick, fifteen-node Daisy drum, and three-node sampler capacities
  each callback. Active tails
  continue processing with no new event, while a silent node that returned
  `CLAP_PROCESS_SLEEP` clears its bus until an event wakes it. The node outputs
  are summed before decode. This preserves independent decays rather than
  truncating one instrument's tail when another instrument plays. Stereo is
  live; both stereo and quad rendering are tested headlessly. Parameter events
  from MIDI OUT lanes are not translated to CoreMIDI or executed by the rack.
- CLAP create/prepare/release/state operations remain on the control side.
  Start/reset/process and the normal stop acknowledgement run on the render
  callback thread. If a dead or reconfigured device cannot acknowledge stop,
  AUHAL is stopped first and a dedicated fallback thread completes the now-
  quiesced processing lifecycle; the control thread does not enter it.

## Native base-patch editor

Window > Membrane Kick (`Command-4`) opens a tracker-native editor with tabs
for active indexed instances, Body/Impact/Strike/Space/Response groups,
draggable strike geometry, five full-patch presets, reset, and bounded audition requests. A new song
shows only `00 MEMBRANE KICK`; number keys select any additional instances.
Space auditions one and `R` restores its kick patch. Tab traverses the
complete accessible control set, arrows edit focused parameters, and Return
activates focused instances and actions.

The window edits each slot's normalized base patch. A base patch defines the
instance's starting/manual parameter state; it is not a replacement for the two FX
lanes. Base changes cross to the audio callback through lock-free scalar
mailboxes and are applied at frame zero. Scheduled tracker FX are appended
after those base events, so an FX event at the same sample has precedence. A
later base edit can deliberately take manual control again. Project persistence
for these patches is not implemented yet.

The editor remains visible in a MIDI-only build so kick patches can still be
prepared. Its first failed parameter, reset, or audition publication reports
`INTERNAL AUDIO OFF` once without flooding the console during a drag; the
edited rack model is retained.

The tracker uses a native editor instead of attaching the CLAP plugin's own
view. This keeps all instances in one consistent s3g Tracker surface, keeps the
normalized base-patch model outside the plugin ABI, and prevents topology
controls such as Output Format from invalidating the prepared graph. It also
preserves the clean rule that Cocoa publishes bounded control data and never
enters CLAP processing.

Snare and tom are intentionally absent from this editor. They require separate
DSP modules rather than additional membrane presets.

Window > Stereo Slice Sampler (`Command-7`) opens the selected sampler. It can
also be opened by double-clicking an indexed sampler or clicking its expand
icon. The editor loads AudioToolbox-supported mono/stereo files, displays the
waveform, creates 1/4/8/16/32/64 equal-slice layouts, edits the base MIDI note
and per-slice reverse flag, and auditions the selected slice. Asset and slice
changes stop transport before the audio graph is safely reconfigured.

The former PSG/YMFM sources and research notes are retained only as archive
material; they are not linked or exposed by the active app.

The persistent bottom Device View follows any indexed instrument. Membrane and
DaisySP instances expose preset selectors, sampler instances expose loaded-file
and note-map status, and MIDI OUT exposes the endpoint and channel owned by
that instance. The displayed two-slot chain reserves the
future insert graph; its FX slots are deliberately labelled empty because
per-instrument insert processing is not implemented yet.

## Safety and telemetry

The shared scheduled-event capacity is 2048. It covers up to 32 tracks, two
parameter actions per tick, retrigger off/on pairs, and the command-facing
16-tick warp collision budget. Larger low-level collisions remain legal for
tests, but live playback fails closed if they overflow. At Play, the live path
also rejects patterns whose maximum warped density could exceed either the
complete audio horizon or one maximum callback block.

The app reports:

- `DROP`: sequencer or MIDI-adapter loss;
- `A-DROP`: a rejected audio batch/event;
- `LATE`: an internal event that arrived behind the callback cursor and made
  the current epoch fatal;
- `CLK`: raw timestamp validation or latched callback/device clock faults.

Any canonical overflow or full audio queue stops scheduling, invalidates the
audio epoch, resets the rack, and attempts immediate MIDI panic. A late
event, aggregate callback-event overflow, or render failure silences that
block, marks the epoch fatal, and resets the graph. If the worker itself falls
behind the rendered device cursor, it does not try to catch audio up: it resets
both destinations, waits through the MIDI cleanup fence, and re-anchors on a
fresh epoch before resuming. An invalid raw clock stops transport. This favors
an explicit reset or interruption over audio/MIDI divergence.

An internal rack instrument requires Core Audio and MIDI OUT requires CoreMIDI.
The scheduler derives this requirement from the default and every explicit INS
cell before Play.

The current MIDI gate is still an adapter-generated cleanup event. Canonical
retrigger and Kill releases are shared, but the configurable short MIDI gate is
not sent to the one-shot membrane rack nodes. Duration must move into the
canonical plan before sustained internal instruments depend on it.

## Build and test

With `s3g-tracker` and `s3g-dsp` as siblings:

```sh
cmake --preset dev-audio
cmake --build --preset dev-audio
ctest --preset dev-audio
open "build/dev-audio/s3g Tracker.app"
```

The preset enables `S3G_TRACKER_BUILD_INTERNAL_AUDIO`. If the CLAP headers are
not found under the known sibling build directories, configure manually with
`S3G_CLAP_INCLUDE_DIR=/path/to/clap/include`.

Two hardware tests are built but intentionally not registered by default,
because sandboxed test runners may not reach macOS audio/MIDI services:

```sh
./build/dev-audio/s3g_tracker_live_audio_integration_tests
./build/dev-audio/s3g_tracker_midi_integration_tests
```

The first enumerates and reselects the active output, attaches a temporary
listener to the tracker's virtual MIDI source, and requires callback progress,
nonzero internal peak, MIDI note 36, and zero late/drop/clock faults. Its presence is
not evidence that a particular machine's hardware path has been exercised.
Configure with
`S3G_TRACKER_ENABLE_COREAUDIO_INTEGRATION_TEST=ON` only on a machine where CTest
has device-service access.

## Deliberate first-slice limits

- Selectable output device and live stereo only; persistent device UID choice
  and live quad/multichannel layouts are next.
- The song instrument index supports five membrane, three instances of each
  DaisySP drum type, three stereo samplers, and eight MIDI instances. Removal,
  reordering, and stack instruments are not implemented yet.
- Native base patches, rack identity, sampler references/slices, and MIDI
  routes are part of the versioned project document; opaque runtime CLAP state
  is not.
- The DaisySP snare and hat voices are prototypes; dedicated tom, cymbal, and
  purpose-built s3g drum modules remain future DSP work.
- Sampler loading, analysis, and project rehydration run in the background;
  equal, transient, and manual slice editing are delivered. Per-hit pitch,
  color modes, and destination latency calibration remain.
- A sibling source checkout is a development convenience. Releases still need
  a pinned `s3g-dsp` revision and coordinated CLAP version.

The next drum-synthesis work is separate tom and metal/noise hat/cymbal modules.
Layout selection, canonical duration, and latency calibration remain parallel
graph work.
