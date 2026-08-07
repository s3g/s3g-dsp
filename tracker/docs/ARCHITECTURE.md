# Architecture

## Boundaries

```text
UI document + commands
          │ whole-pattern publication
          ▼
sole sample-domain Sequencer worker ◄──── AUHAL block frame/host clock
          │ one ordered canonical slice
          ├──────────────► timestamped CoreMIDI ───► Superior Drummer
          └─ fixed SPSC batches ─► Core Audio callback
                                      │
                                      ▼
                                  audio graph
                                      ├─ capacity for five membrane-kick nodes
                                      ├─ five DaisySP drum families
                                      ├─ capacity for three stereo samplers
                                      ├─ curated embedded CLAP
                                      └─ post-decode MAIN OUT gain
```

`ScheduledEvent` is the canonical scheduler boundary. It carries absolute and
block-relative sample time, a stable nonzero note ID, normalized velocity or
parameter value, track, channel, note, choke metadata, event kind, and an
adapter destination. Each track owns a zero-based default instrument plus an
independently polymetric optional `INS` override column. Explicit cells can
update memory on note rows or rests. Rack nodes `0..4` are independent
membrane-kick instances, nodes `13..27` are the five three-instance DaisySP
drum families, nodes `28..30` are stereo samplers, and nodes `31..38` identify
independent MIDI OUT devices. IDs `5..12` are inactive reservations left by
the archived chip experiment; they are not selectable or routed.
The scheduler derives adapter destination from the resolved instrument kind;
there is no user-facing per-track `Both` switch.

The document pattern bank also owns each pattern's console aliases and lane
pitch memory. The UI copies those authoring maps into the active command
session on selection and back into the bank before publication or save;
aliases are never shared across patterns with different track layouts.

Each track also carries a bounded event-stage velocity scale. The scheduler
applies it only to newly emitted Note On velocity before the canonical event
fans out, so MIDI and internal synthesis receive the same normalized value.
It does not modify Note Off, parameter events, or an existing audio tail.

Authored membrane FX store the `kTrackInstrumentNode` sentinel. Before each
tick emits events, the scheduler reads `INS`, updates remembered instrument,
and resolves lane-relative FX and the next onset to a concrete node. An
active-note record retains the node that received its onset, so changing `INS`
before a retrigger releases the old node before the new node receives onset.
On a rest, Global/Channel FX follow current instrument memory while a future
Note-scoped FX follows the node owning the active note. MIDI note pitch remains
the NOTE cell's independent value.

Raw MIDI bytes, CLAP events, and instrument-node render events remain adapter
formats. Parameter actions also declare global, channel, or note scope;
adapters use wildcard fields for a global action rather than accidentally
targeting channel/key zero. Concrete nodes cache native parameter ranges on
the control side and translate the canonical normalized scalar at the adapter
edge. That is the extensible event contract, not a claim that every node
implements every scope: the current membrane catalog advertises Global only,
and command validation rejects its Channel or Note scope requests.

`TrackerPlaybackEngine` owns the only live `Sequencer`. Its dedicated worker
reads the latest AUHAL callback block `(device frame, host time)`, schedules to
an audio horizon of `max(20 ms, two callback budgets + 12 ms)`, and partitions
each completed canonical slice by resolved instrument kind. Each slice is
offered to a fixed-batch SPSC queue as one transaction; that queue stores only
internal-instrument events. MIDI OUT note events wait in a bounded staging queue and
are submitted only through the shorter paired-MIDI horizon of
`max(20 ms, one callback budget + 12 ms)`, with CoreMIDI host timestamps
derived from the same device-frame origin. A dependency-free MIDI-only build
retains its fixed 20 ms host-time horizon. The callback consumes due internal
events, assigns exact block offsets, and renders the embedded graph. It never
owns or mutates the document and never performs a pattern-vector replacement.
The callback budget is resolved from the opened device's reported maximum and
fixed/variable buffer-frame configuration, so neither live horizon assumes a
particular hardware buffer size.

This arrangement deliberately keeps live whole-pattern replacement on the
non-realtime sequencer worker while making Core Audio the sole clock. A change
becomes audible after the already-published audio lookahead, whose duration is
the formula above rather than a fixed 20 ms.
Cocoa polls atomic playhead and telemetry snapshots at 30 Hz; that timer never
advances musical time. If AUHAL timestamps become unavailable/discontinuous,
the scheduler event buffer overflows, or a complete audio batch cannot be
published, playback invalidates the epoch, resets internal voices, and sends
MIDI panic rather than allowing the two destinations to diverge.

Audio cannot catch up through device frames that the worker failed to commit.
If the callback finds a late event, exceeds its aggregate event budget, or
cannot render, it silences the block, marks the epoch fatal, and resets the
graph. If the worker detects that the device cursor passed its scheduled
cursor, it invalidates the epoch, resets audio and MIDI, waits through the MIDI
cleanup fence, and re-anchors before playback resumes. Clock validation faults
fail closed and stop instead of publishing an inferred clock.

One UI-owned `TrackerViewState` contains the command engine's active-pattern
`TrackerSession`, an ordered stable-ID `PatternBank`, and sibling
`InstrumentRackState`. Active pattern and per-lane note-entry memory are
synchronized into the bank before selection, persistence, or playback
publication. The main NOTE/INS/VOL and FX grid, contextual VOL/V1/V2 Envelope, retained
Rhythm Geometry and Membrane Kick windows, Song editor, right instrument
toolbox with console-output readout, bottom Device View, and transport-area
live-code entry
all operate on that state. Validated pattern changes publish through the
scheduler boundary; rack base patches use their bounded audio-control
mailboxes. Auxiliary windows never own a sequencer or clock. Console commands
currently apply transactionally to a candidate session; the next
controller/undo step will route the same musical operations through a shared
semantic action registry.

MIDI note cleanup uses one pending deadline per physical MIDI-device slot and
note while preserving the owned endpoint/channel identity and canonical stable
note ID through scheduling. A retrigger emits a canonical
off for its prior identity immediately before the new onset; the MIDI adapter
keeps their timestamps strictly ordered and establishes a fresh gate deadline.
A Kill cell carries the latest active ID and sends an off for its remembered
key. Stop sends All Notes Off immediately and again at a cleanup fence no
earlier than 28 ms from the request, 8 ms beyond the latest submitted MIDI
timestamp, and any prior fence. A MIDI instrument's endpoint/channel selection
waits for that final old-route sweep; changing a track's default instrument stops transport,
performs the same overlap-safe cleanup, and then publishes the new pattern.
Duration and choke-group policy remain graph-level work.

In the live-audio build, instrument choice is also an availability contract.
Any internal default or INS cell requires live audio; any MIDI OUT default or
cell requires CoreMIDI. The plain `dev` build has no internal graph, so
internal instruments are silent. Parameter actions are graph events, not MIDI
automation: internal nodes execute supported actions while a MIDI OUT lane
merely advances its FX column state.

The live slice still synthesizes the configurable short MIDI gate in the
CoreMIDI adapter. Explicit canonical retrigger/Kill NoteOff events fan from the
shared stream, but the adapter-generated 90 ms gate release is not sent to the
one-shot membrane rack nodes. Canonical duration expansion is required before
a future sustained internal voice relies on that gate.

Each track owns independent NOTE, INS, and normalized VOL columns plus two typed FX
action/value pairs. `INS` Empty and Previous retain instrument memory while an
explicit instrument replaces it; a muted INS column retains memory but still
advances phase. All FX columns likewise advance independently. Empty action
cells retain memory without emitting; Previous executes the remembered action;
explicit actions update memory and execute. Value Previous retains its
independent normalized value. Duplicate resolved targets resolve
deterministically with FX2 winning, and equal-sample order is `NoteOff`,
parameter actions, then `NoteOn`. Authoring uses stable catalog keys and a
lane-relative node sentinel; canonical output contains concrete node/parameter
IDs only. INS memory itself emits no callback event and does not increase the
fixed event budget.

## Hybrid rack and editor state

`TrackerAudioEngine` owns five independent `MembraneClapNode` instances, five
native `Sn76489PsgNode` instances, and three `Ym2151OpmNode` instances. A
single `EmbeddedClapEntrySession` initializes the renamed compiled-in CLAP
entry once; all five plugins are created under that session, destroyed in
reverse order, and only then is the entry deinitialized. Sharing entry
lifetime avoids pretending five linked instances are five plugin libraries.
It does not share their voice, parameter, or state data.

Every internal rack slot is visited each callback and its 16-channel ACN/SN3D result is
summed into the master bus. Active eventless processing is intentional: a
floor-tom tail must continue while a kick receives the next block's events.
Slots that have reported `CLAP_PROCESS_SLEEP` remain parked until an event.
Stopping/resetting the graph affects all slots together, while ordinary note,
parameter, and serialized state operations remain slot-specific.

After layout decode, a lock-free MAIN OUT target mailbox drives an
approximately 5 ms render-thread gain ramp. The gain advances exactly once per
sample frame and is shared by every stereo or quad output channel. CoreMIDI is
fanned out before this graph and therefore bypasses MAIN OUT. The UI peak
meter consumes the maximum rendered hardware-output peak accumulated between
display snapshots.

There are intentionally no per-track post-DSP faders yet. Several tracker
lanes can address the same internal instrument node, and those node HOA
buses are summed before decode; that topology cannot truthfully recover a
rendered lane level. True track gain, pan, sends, inserts, and meters require
explicit lane-isolated render buses feeding the layout-aware master.

The native Membrane Kick window edits `InstrumentRackState`, whose
normalized base patch is application-owned state intended for future document
persistence rather than CLAP GUI state. Changes cross to the callback through
bounded lock-free scalar mailboxes. Dirty base parameters are inserted first at
frame zero; authored FX events are inserted afterward, so a scheduled
frame-zero FX value wins that callback without changing the base-patch model.
The editor is for kick-instance setup, strike geometry, audition, and reset; FX lanes
are the sample-timed compositional/performance layer.

The DaisySP nodes follow the same bounded-instance rule without involving
CLAP: every indexed device has isolated DSP, parameter mailbox, event buffer,
and tail state. The sampler similarly owns a fixed 32-voice pool per instance,
but references immutable decoded assets. Changing an asset or slice map stops
transport and reconfigures the prepared graph outside the audio callback.

The membrane editor is deliberately tracker-native rather than an embedded copy of the
membrane plugin's Cocoa editor. It presents only instances added to the song
index while the audio engine retains bounded capacity for five voices, keeps
normalized parameter metadata in the tracker model, avoids exposing topology
controls such as CLAP Output Format, and does not let UI code enter plugin
processing. CLAP remains the reusable DSP/state adapter underneath it.

Snare, tom, hat, and cymbal modules remain separate DSP work; the kick editor
does not present parameter presets as completed versions of those instruments.

The bottom Device View is the first visible graph-device boundary. It follows
the selected indexed instrument, exposes Membrane/Daisy presets, sampler
status, or MIDI-owned endpoint/channel controls, and displays a route to MAIN
OUT. Its two effect
slots are intentionally empty placeholders until the graph owns real prepared
insert nodes, latency, and state.

## Core model

Do not reproduce the Max script's monolithic global model. Split:

- `PatternDefinition`: musical cells and per-column length/stride/direction.
- `PlaybackState`: playheads, palindrome direction, deterministic PRNG state,
  active notes, song position, and pending launches.
- `ProjectDocument`: patterns, song form, articulation maps, controller maps,
  audio graph state, and UI state.
- `PlaybackPlan`: immutable, fully validated state consumed by the audio or
  scheduling thread.

This separation lets a saved pattern start deterministically while an explicit
session snapshot can preserve phase.

## Timing

Audio device frame time is authoritative in the internal-audio build. The
dependency-free MIDI build retains the host-time scheduler as a deliberate
development/CI fallback. Musical position uses rational or fixed-point
beat/tick values; a 64-bit absolute sample cursor anchors emitted events.

The baseline uses four tracker ticks per beat. Swing preserves the duration of
each two-tick pair. Note-source and probability gates resolve in the Sequencer;
microtiming, ratchets, flams, delays, ghosts, and stutters then expand into
bounded sample-time events before output, never through a UI timer.

The current bounded scheduler covers microtiming, ratchets, flam, delay,
stutter, accent, and ghost. A fixed 8192-entry timeline retains future events
across audio/MIDI blocks and a 2048-event due slice fails closed on overflow.
MT shifts each row's release/parameter/onset bundle together. If Kill or
retrigger reaches a primary note before its delayed onset, the pending onset
and note-scoped controls are canceled in-place rather than creating an orphan
voice. RR/DL/ST/GL spacing uses the straight nominal tempo tick, preserving the
v8 contract while swing and functional warps position primary tracker rows.
The nominal Sequencer resolves `OF` then `RP`, followed by `PR`, `SK`, and `EU`,
before any accepted Note On enters this timing timeline. This keeps note
identity, active-owner release, deterministic RNG, and block partitioning
authoritative in one place. Bounded timing expansion and the logical-tick Song
boundary are enabled by default; an explicit build option retains the nominal
fallback for comparison.
Secondary ratchet/stutter onsets currently target one-shot drums, the sampler,
and MIDI's per-key retrigger/gate path. A future sustained-voice instrument
must define explicit release/cancellation semantics for those secondary IDs.

With Song transport enabled, the worker owns `SongPlaybackPlanner` beside the
scheduler. An allocation-free logical-tick observer applies row transitions
after a complete tick bundle is admitted and before another tick is generated.
It retimes the immediately following interval, overlays the row's 32-bit mute
mask, and relaunches column heads without resetting active-note ownership, FX
memory, RNG, or the absolute clock. Repeats preserve polymetric phase. The
stop-after-boundary path drains all delayed final-tick events before the Song
session is released.

Before Song playback the engine resolves stable pattern IDs to a compact set of
normalized prepared slots. A two-swap active-hole pool keeps those slot indices
stable while making boundary activation allocation-free. Playback/skip storage
is sized to the set's maxima, timing reachability is cached, and removed lanes
hand their active owners to a fixed 32-entry release queue for the first target
tick. The one live timeline, absolute clock, note-ID generators, FX/RNG recall,
and audio epoch are never exchanged. Pattern selection still requires stopped
transport, and edits received during an active Song are deferred until its
frozen prepared set is released.

After pair swing, a fixed-capacity `TimingWarpStack` maps cycle phase through
serial exponential, stepped, and Euclidean transforms. Each transform can be
limited to a phase segment, repeated inside that segment, and blended with the
incoming ramp. Endpoints remain fixed and event time is derived from adjacent
warped phases, so block size does not alter the result. The first integration
is global; per-lane timing fields are a later model extension. See
[Timing Warps](TIMING_WARP.md).

Stepped maps may deliberately collapse ticks onto one sample. The core retains
stable tick order, emits only the fixed-capacity prefix, and reports overflow.
The shared capacity is 2048 events: the command-facing live limit fits the
32-track, two-FX bounded model, and more extreme low-level
transport definitions stop both destinations on overflow. Before Play, the
live-audio path bounds warped ticks across both its complete lookahead and one
maximum callback block, then rejects patterns that could exceed the canonical
or callback budgets. Runtime checks remain authoritative and fail closed.

Every lane gets deterministic random state. A project can therefore reproduce
playback or intentionally request a new seed.

The AUHAL adapter enumerates output devices and opens the selected stereo
output. Device reopen is serialized on the playback worker after transport and
MIDI cleanup; future persistence should store Core Audio UID rather than the
ephemeral numeric device ID.
The reused `s3g-dsp` wrapper's timed callback passes raw AUHAL host time,
sample time, and validity flags into the tracker before rendering. The tracker
preflights both clocks for presence, forward progress, and callback-to-callback
continuity before publishing a monotonic device cursor; the scheduler uses the
actual device sample rate. After-callback telemetry adds device, callback, and
render-fault latches, but is not used as a substitute for timestamp preflight.
Missing or discontinuous HAL timestamps are transport faults; the tracker does
not silently adopt the wrapper's emergency wall-clock fallback.

## Thread ownership

| Context | Owns | Must not do |
| --- | --- | --- |
| Main/control | Documents, rack base-patch state, undo, commands, GUI, device configuration, and CLAP discovery/init/destroy/activate/deactivate/state | Touch live render state in place or prepare graph replacements on an arbitrary worker |
| CoreMIDI callback | Timestamp and parse input, enqueue bounded messages | Edit documents, allocate unbounded work, send feedback |
| Sequencer/MIDI worker | Own the sole live Sequencer, consume serialized pattern replacements, follow the callback-derived AUHAL horizons, publish complete audio batches, and submit future MIDI | Touch Cocoa, edit the UI document, or enter CLAP processing |
| Core Audio callback | Publish block clock, consume due fixed batches, assign frame offsets, run CLAP start/reset/process and the normal stop acknowledgement, scatter prepared stereo output | Lock, allocate, use strings/files/devices, mutate patterns, or advance a second sequencer |
| Worker pool | Sample decode/resample, waveform analysis, and autosave | Publish partial assets into the render graph |
| Feedback service | Coalesce BU16 LEDs, E16 rings, and Keychron state | Delay musical notes |

Topology or channel-layout changes build and prepare a replacement graph off
the audio thread, then swap it at a safe boundary.

## s3g-dsp reuse

The strongest existing pieces are:

- `standalone/host/s3g_embedded_clap_host.*`
- `standalone/audio/s3g_coreaudio_output.*`
- the lifecycle, CoreMIDI, and device-persistence patterns in the No Input
  Mixer standalone app
- `plugins/common/s3g_clap_gui_param_queue.h`
- the Cocoa palette/layout conventions in `plugins/common`
- `s3g_euclidean_rhythm`, scale helpers, sample/loop processors, and
  `s3g_mc_to_stereo` / `s3g_mc_to_quad`

The current CoreMIDI output in the No Input Mixer app sends controller feedback
with timestamp zero from a UI service path. That code must not be reused for
musical notes. Tracker needs its own timestamped performance queue and sender.

Do not add the entire `s3g-dsp` root with `add_subdirectory`; its root declares
many products/tests and may fetch optional dependencies. When reuse begins:

1. Add a pinned `external/s3g-dsp` submodule plus an `S3G_DSP_ROOT` override
   for sibling development.
2. Expose only the `dsp` headers needed by a target.
3. Add the standalone host subdirectory only for a target that embeds CLAP.
4. Consume one coordinated pinned CLAP version across both repositories.
5. Longer term, export `s3g::dsp`, `s3g::standalone_host`, and a neutral Cocoa
   style target from `s3g-dsp`.

## Multichannel contract

Graph ports declare their layout explicitly. Buffers are planar and allocated
during prepare. Nodes advertise supported configurations and latency; parallel
paths are compensated. The first internal master layouts are stereo and quad,
then discrete/ambisonic layouts already used by `s3g-dsp`.

Changing a CLAP audio-port configuration requires a stopped/deactivated node
and a prepared graph rebuild. Native `.s3gt` projects persist the rack's
normalized base patches and tracker-owned topology. A future schema may also
store an optional CLAP state blob per embedded node, but a plugin blob will
never become the tracker project format; runtime CLAP state is not currently
serialized.

The optional internal-audio target is now both a headless contract proof and a
live stereo path. Canonical internal events address five instances created
from one compiled-in membrane CLAP entry at exact sample offsets. Their
independent fixed 16-channel ACN/SN3D outputs are summed and decoded to stereo
live, while stereo/quad, per-slot state isolation, simultaneous mixing, and
eventless tails are covered headlessly. Device selection and live
quad/multichannel output are the next graph/device steps; they do not require
a second sequencer.

## Persistence and safety

- New schemas are versioned and strictly validated.
- Writes use a temporary sibling file, flush/sync, and atomic rename.
- Autosave never overwrites the last known-good recovery file directly.
- The Max file formats are not imported; new documents contain musical content
  separately from optional session/playback state.
- Stop, instrument/endpoint change, destination loss, and app termination all run an
  overlap-safe note cleanup/panic path.
- Fixed-capacity queue overflow is counted and visible in diagnostics.
