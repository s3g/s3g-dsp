# Sample Neon 32

`s3g Sample Neon 32` is a 32-slot Sample-family instrument designed around the
factory MIDI map of the Reloop Neon. Version 0.26.0 exposes one CLAP instrument,
`s3g Sample Neon 32`, with a fixed 32-channel output port. The default Stereo layout
mixes cells onto channels 1–2; unused channels remain silent.

Double-click any slider track to restore that control's default. This includes
the inspector's playback, stack, character FX and resample controls, plus the
mini-NEON's contextual TRAX/LOOP sliders and master gain. Only that control is
reset; sources, playback mode and other settings are kept. Trim resets retain
the zero-crossing setting and recalculate transient/beat-grid slices as needed.
This update also keeps active audition/stack sources alive across host audio
reactivation when a take is cropped or a layer is replaced.

## LANES playback (0.26)

Choose **PLAYBACK → LANES** for continuous sample playback with direct movement
through this pad's stack (up to 32 layers). Unlike Motion or Grains, this method
does not emit short source windows: one continuous read head keeps moving while
you change the layer being heard. Nothing from adjacent pads is mixed in.

- **MANUAL** is the default. **LAYER POS** moves continuously from layer 1 to the
  last layer; **PREVIOUS LAYER / NEXT LAYER** land on individual layers without
  restarting playback. The PLAY page also exposes Layer Pos.
- **CROSSFADE** blends neighboring loaded layers with equal-power weights;
  **JUMP** chooses the nearest loaded layer. **LAYER SLEW** smooths transitions
  (1–100 ms time constant). Missing layers are skipped by the audio reader.
- **SPEED** changes the continuous read rate, separately from layer navigation.
  Each layer uses its own trim; the primary/first loaded layer's trimmed length
  sets the 1x clock. Different-length layers share normalized progress, so their
  effective rate/pitch differs. Tune is additional rate transposition, not a
  pitch-preserving stretch. Forward, Reverse and both Ping-pong directions work.
- **LOOP JOIN** overlaps the end/start of each loop, shortening its repeating
  period by the overlap amount; Ping-pong does not need that join.
- **PATH** optionally moves Layer Pos along the existing shared breakpoints.
  Path Cycle uses seconds in FREE or beats in HOST. Manual navigation and the
  read speed remain separate from this path's cycle speed. Returning to MANUAL
  restores the saved manual position; touching LOOP takes over from the live
  path position. Selecting a named path shape does not change manual position.
- **HOLD** runs while held (plus Release); **TOGGLE** runs until the next press;
  **ONE SHOT** uses Shot Len. Attack/Release shape the whole gesture. FREE works
  with transport stopped; HOST pauses/resumes with transport. STOP/chokes stop
  immediately. CHOP still auditions the selected source directly.
- On **EDIT → PLAYBACK SETTINGS**, **LOOP** controls Layer Pos and **TRAX**
  controls Speed. LOOP uses eight steps per layer for Crossfade, one per layer
  for Jump; Shift gives fine movement. LOOP press auditions the pad.

Waveform follow tracks the layer blend without changing the selected edit
layer. Shared phase, interpolation, blend weights and envelopes preserve
multichannel/ACN-SN3D channel relationships. Use Character FX for filtering.
This is the focused Lanes playback model, not the standalone instrument's
independent per-layer speed/stretch/nudge transports.

LANES and its new controls use state version 18, including copy/paste and Reset
All. Existing sounds continue saving v14/v15/v17 when no new Lanes data is
needed. Older plug-in binaries cannot read v18; old projects remain readable
by this version. These controls are state-backed, like Neon's other advanced
playback controls, not new host-automation parameters.

## Motion, Grains and shared stack paths (0.25)

The signal organization is **pad stack → playback technique → Character FX →
output**. Layer movement and movement within a sample are separate. Selecting
Motion or Grains commits to that technique; opening a detail view does not
change the playback engine. No adjacent pad is mixed into this pad.

In EDIT, use **EDIT VIEW** for the compact **MOTION DETAIL**, **GRAINS DETAIL**,
and **STACK PATH** views. Technique detail entries are available only for the
active technique. Ordinary PLAYBACK SETTINGS and the hardware encoder mappings
remain available for the familiar position, cycle, size and density controls.

- **Stack Path:** select STACK SCAN for Motion/Grains/Stretch/Wavesets, or PATH
  navigation for LANES. The SHAPE menu on either Stack
  page offers MANUAL, RAMP UP, RAMP DOWN, TRIANGLE, SINE, SQUARE and WANDER.
  Every choice uses the visible breakpoints for playback; there is no separate
  PRESET path engine. Selecting a named shape regenerates its points and clears
  Curve. Selecting MANUAL keeps the current shape and unlocks direct editing.
  Drag nodes below the waveform horizontally for time and vertically for layer
  position, or use the selected point's TIME and LAYER POS sliders. Named shapes
  keep these coordinates read-only. End times stay at 0/100%; points cannot cross.
  Use 2–32 points; Triangle/Wander need at least 3, Square 4 and Sine 9.
  Changing point count regenerates named shapes or resamples the Manual curve.
  RESET PATH rebuilds the selected shape (a ramp when Manual), keeping the
  point count, phase and clock. PHASE offsets the cycle;
  CURVE bends interpolation. CROSSFADE blends adjacent layers within this pad;
  JUMP picks the nearest layer. Grains can advance the path by TIME or GRAIN
  EVENTS (32 emissions per cycle, including doublets). The selected edit layer
  stays independent of the playing layer.
- **Motion / Articulation:** CONTINUOUS, PACKETS or MOTOR. Packet Rate and Duty
  set the inner pulse; Motor Rate, Shape and Symmetry add a slower outer
  envelope. Linear, Rounded, Exponential and Plateau shapes use the same motor
  function as Sample Motion. Short joins suppress abrupt packet edges.
- **Motion / Movement:** the original Forward/Reverse/Bounce/Wander paths plus
  Hover, Mirror, Zigzag and Moving Loop trajectories, with Locus, Field,
  Travel and Jitter. Motor's falling side reverses bounded trajectories;
  Forward/Reverse retain their one-way travel. Join Window adjusts the source
  window from 5–500 ms (legacy default 40 ms).
- **Motion / Events:** Smooth, Freeze, Iterate, Doublets or Bounce, with event
  rate, repeats, source step, pitch spray, level variation and acceleration.
  These are bounded per-pad window events, not another top-level playback mode.
- **Grains / Source:** Freeze (the existing position cloud), moving Scan,
  random Cloud or equally divided Slice regions. Spray Bias chooses Behind,
  Around or Ahead. Grain Tune is independent of source scanning. Scan Cycle
  uses seconds or host beats. Density x extends free-running density to
  0.1–160 Hz, while retaining the familiar base Density/Interval controls.
- **Grains / Window:** Legacy envelope or Parzen, Sine, Hann, Triangle and
  Gaussian windows, sharing the exact window math with Sample Grains. Includes
  envelope skew, size/level variation, timing scatter and Size x (up to 4 s).
- **Grains / Process:** Ordinary, loudness-ranked Sorter, anchored Stutter,
  progressively shortened Shrink or probabilistic Doublets. Amount controls
  the mutation strength/repetition/probability; Regions sets the Slice/Sorter
  partition. Doublets can repeat the source position or advance it by the
  delayed onset time.

All timing, random decisions, envelopes and source offsets are linked across
channels. ACN/SN3D channel order and relative gains are preserved. These controls
are part of each pad's saved sound, copy/paste and Reset All. Double-click the
new sliders to restore their defaults. Old states load with neutral defaults;
new non-default pre-Lanes family settings use state version 17 (older binaries cannot
read that extended state). Like existing advanced Neon controls, these are
state-backed editor settings, not newly exposed host-automation parameters.

Version 0.25.1 preserves old authored breakpoint paths as Manual. Older
Forward/Reverse/Bounce paths become Ramp Up/Ramp Down/Triangle points; old
Wander is sampled into 32 points (a close approximation, not bit-identical).
Opening the editor alone does not rewrite the sound.

This is a Neon-specific integration, not full binary parity with the three
standalone instruments. It retains one held gesture per pad and 32 overlapping
sample voices per pad; dense, long grains can voice-steal. It does not import
keyboard mono/legato modes, dynamic per-grain output allocation, independent
stereo randomization, Motion's native-rate oscillator/seeded Drunk/turn-triggered
or Routed Iterate engines, or Lanes' independent per-layer speed/stretch/nudge
transports. Motion's event models operate on Neon's window scheduler. Grain
Slice uses equal regions; Slice Sequence still uses the primary layer's authored
slice markers. HOST gates/aligns the gesture clock; packet/motor/event rates
remain Hz rather than musical divisions.

## Multichannel sources and routing

Load mono, stereo, quad, octo or 4/9/16-channel ambisonic files. Select the
global output layout in the header and each cell's **Format** and **Output**
in the inspector. Four-channel files default to discrete quad; explicitly
choose **ACN / SN3D** for 1OA. Newly loaded 9- and 16-channel files default to
ACN/SN3D. Channel count cannot verify a file's actual encoding: convert other
conventions before loading. No FuMa, N3D, channel reordering or automatic
ambisonic decoding is performed.

| Output layout | Bus width | Available buses |
| --- | ---: | ---: |
| Stereo | 2 | 1 |
| Stereo stems | 2 | 16 |
| Quad | 4 | 8 |
| Octo | 8 | 4 |
| 1OA ACN/SN3D | 4 | 8 |
| 2OA ACN/SN3D | 9 | 3 (channels 28–32 unused) |
| 3OA ACN/SN3D | 16 | 2 |

Bus channels are consecutive and one-based in the editor. All cells default
to bus 1. Discrete sources may occupy a wider discrete bus without reordering
(remaining channels are silent); ambisonic sources require an exact order
match. An incompatible format, width or bus is silent and visibly flagged.
There is no implicit downmix or wraparound routing. Stereo pan is disabled
for spatial sources.

Trim, pitch, slice boundaries, reverse, loop, envelopes and grain randomness
are linked across every source channel. Waveforms can be combined, isolated to
the first channel, or stacked and labelled CH or ACN. Every playback technique uses only its selected cell;
Motion scans, grain positions, envelopes and timing stay linked across channels.
Wavesets uses channel-dependent cycle analysis and is discrete-only.
Mapped chops retain the source format, Character FX settings and route.

The editor uses shared Fira Code body/title metrics, centered 15-pixel menu
fields with 18-pixel popup rows and a compact hardware map. Instructional helper
paragraphs are removed; parameter labels, routing warnings and operation status
remain. Standard proportional scaling remains
available from 65% to 200%.

## Pad stacks and storage (0.23)

Each of the 32 pads can hold up to 32 source layers. Open **EDIT VIEW → SAMPLE
STACK** or **PLAY → EDIT SAMPLE STACK**. ADD LAYER appends; LOAD replaces the
selected layer. Dropping multiple files while the stack editor is open appends
them to the selected pad. REMOVE LAYER removes only that layer, never its file
on disk. Layer 1 is the primary. All layers in a stack must have the same channel
count; source format and output routing remain pad-wide. ACN/SN3D channels are
never independently selected, reordered, or randomized.

**SOURCE MODE** and **PLAYBACK** are separate:

| Source mode | Selection |
| --- | --- |
| PRIMARY | Layer 1, regardless of the layer displayed for editing |
| SELECTED LAYER | The layer selected in the editor |
| VELOCITY | Equal velocity bands across the stack, quiet to loud |
| RANDOM / TRIGGER | One choice per pad strike, held throughout that gesture |
| STACK SCAN | Continuous traversal with Motion, Grains, Stretch, or discrete Wavesets |

STACK SCAN has its own cycle and breakpoint shape (Manual, ramps, Triangle,
Sine, Square or Wander). Its clock
follows the pad's FREE/HOST choice: FREE runs with transport stopped; HOST
follows the host clock. The existing Motion path or Grains position still
controls time *within* each source. Overlapping, channel-linked grains blend
neighboring stack layers with linear weights; each voice retains its source
until it ends. HOLD release uses the existing gesture release envelope.
One Shot uses the existing Shot Length; Toggle continues until tapped again.
The stack grid shows the current blend without changing the editing selection.
The waveform's **FOLLOW STACK / EDIT LAYER** menu defaults to following the
playing source in every source mode. Primary, Selected Layer, Velocity and
Random / Trigger show the actual triggered layer, including its trimmed waveform,
overview, channel rows and source-matched cursors. Overlapping Sample hits follow
the most recently triggered voice still playing; when it ends, an older surviving
hit becomes visible. The stack grid's playing indicator follows the same source.
During Stack Scan, main waveform, overview, channel rows, and source-matched cursors fade
between the neighboring layers using the audible scan weights. Each trimmed
layer is aligned to the same normalized timeline, including different source
lengths. The header identifies both layers and blend percentage. Following never
changes the editing selection; trim/cursor editing is disabled on the followed
display. Choose EDIT LAYER for a fixed, editable waveform. CHOP and RESAMPLE
always keep their own fixed source views. Stopping or isolated audition restores
the selected-layer view. This display choice is instance-local, like channel view.
Start/End, slice map, and source-editing settings are
kept per layer. Gain, technique, envelopes, FX, and output route are pad-wide.
For Sample, Motion, Grains, Stretch and scanning Wavesets, EDIT's AUDITION
(or LOOP push on the stack page) isolates the displayed layer;
performance pads use the saved source mode and play the complete stack.
Stretch keeps its within-source duration separate from the stack cycle; both
neighboring grains receive the same pitch-compensated window duration.
Wavesets offers PRIMARY or STACK SCAN. In scan mode, two cycle processors blend
adjacent layers; each incoming layer begins its own cycles at its trimmed start.
Analysis runs on the file worker, never in the audio callback. Wavesets remains
disabled for ACN/SN3D. Its scan supports FREE/HOST, HOLD/TOGGLE/Shot Length and
gesture attack/release. Layers without usable cycle analysis remain silent.
Slice Sequence always uses layer 1 and its slice markers, independent of the
edit layer; it does not scan the stack. Choosing Sample disables Stack Scan.

On the stack editing page, LOOP chooses the edit layer and TRAX adjusts the
stack cycle (seconds in FREE, beat choices in HOST). Pad-bank selection is
unchanged. Copy/paste copies the whole stack and its settings; immutable PCM is
shared, but later edits are independent.

CHOP's **DESTINATION** chooses **PAD CELLS** or **ONE PAD / LAYER STACK**.
**START PAD** places slices on consecutive pads from the chosen A1–D8 cell,
across bank boundaries but never wrapping past D8. **TARGET PAD** chooses the
one cell that receives all slices as layers. **AUTO / EMPTY** retains the
first-empty-pad behavior (skipping occupied pads). The menu shows pad occupancy;
the Assign buttons name their destinations, and **ALL >** lists the exact range
or ranges before committing. Occupied destinations or insufficient room disable
Assign All without preventing Assign One when one slice still fits.

Choose the source's **A1 / REPLACE** entry (using its actual pad name) to replace
its entire stack: with pad distribution, slice 1 occupies that pad and the rest
continue consecutively; with layer distribution, all slices occupy that pad's
new stack. The display explicitly warns **SOURCE ... WILL BE REPLACED**.
Other occupied pads cannot be selected or overwritten. Destination/distribution
are snapshotted when Assign is pressed and revalidated before changing audio.
These edit-action choices are instance-local and reset to Auto / Empty and Pad
Cells on Reset All or loading a set/project state; they do not change the state format.
All slices become standalone cropped audio. Capacity and embedded-audio budget
are checked before assignment; existing unrelated cells are not overwritten.

The title-band **PROJECT / LINK / EMBED** menu applies to all pad layers:

- PROJECT is the new-instance default. Files are collected asynchronously into
  the saved REAPER project's media directory, under `s3g Samples`, using the
  shared verified-copy and project-file registration services. Generated pad
  audio is exported as channel-preserving float WAV before collection. Recall
  uses project-relative references; REAPER Save As needs its copy-media option.
- LINK keeps external file references. Keep referenced files available.
- EMBED stores decoded PCM inside host plug-in state and `.s3gneon` sets, never
  in the installed plug-in. Shared PCM is stored once, with a 256 MiB unique-PCM
  limit. Large embedded sets can make host saves/undo snapshots expensive.

Pathless audio and the unassigned resample review take remain embedded for
safety. An unsaved/unsupported project leaves PROJECT collection pending;
imported files stay linked until collected. Missing locators survive recall
and can be replaced through LOAD. Existing sessions retain their LINK plus
generated-audio embedding behavior. Extended stack/storage states require
0.23 or newer; unchanged single-layer LINK sessions retain version-14 state.

Version 0.22.4 corrects the bank lamps after EDIT B → SAMPLE A → SAMPLE B.
Ordinary bank changes no longer re-send the four-deck sampler-mode setup or
mode-lamp commands. Feedback clears the inactive SAMPLE-bank and editing-deck
lamp addresses before lighting the selected bank, after any mode commands and
before repainting pads. SAMPLE's second layer correctly uses the deck addresses.
The change is confined to hardware feedback; musical notes, velocity, held-pad
releases, host parameters and saved state are unchanged.

Version 0.22.3 introduced restoration of the hardware's primary SAMPLE mode
on all four decks when entering SAMPLE/performance (also on bank changes
until 0.22.4), followed by restoration of the selected bank. This uses the
separate `93..96 0D 7F` sampler-mode triggers in
the [Reloop MIDI map, page 9](https://www.reloop.com/media/custom/upload/Reloop-NEON_MIDI-Map.pdf),
not just the SAMPLER lamp. A bank's remembered editing page should no longer
take over a performance. Explicit mode-button changes still enter the editing
pages. Mode commands are not repeated during pad-lamp settling retries or
sent when relinquishing hardware ownership. Hardware/firmware acceptance of
the documented trigger remains a separate check from the automated tests.

Sample Neon remains the sole hardware-feedback owner. In the Utility/Tracker
chain use **TRACKER + SAMPLE NEON** and **NEON OWNER: ON**; **NOTES ONLY**
intentionally cannot synchronize hardware pages through the instrument.
No Utility/Tracker binary, state layout or parameter ID changes are required.

Version 0.22.2 corrects native NEON velocity decoding, including direct hardware
input and editing/audition controls bridged through Utility Neon MIDI 0.1.1.
The NEON sends measured velocity as a CC before a fixed-127 pad note; this
was incorrectly treated as aftertouch. Both plugins now pair these messages
using the same bounded decoder, including across host blocks. Actual aftertouch
remains separate. Enable the hardware's velocity mode with **SHIFT + SAMPLER**;
use **EDIT → SOURCE/TRIM → VELOCITY → PAD VELOCITY** for dynamic cell loudness.
No state/parameter changes; Tracker's normalization is unchanged. See the
[Utility velocity notes](../clap_utility_neon_midi/README.md#hardware-velocity).

Version 0.22.1 adds the **WAVE** menu in the waveform panel header, on every page:

- **COMBINED PEAKS** (default): one min/max envelope spanning all channels.
  This is the previous mono/stereo view, not an audio sum, average or dynamically
  chosen loudest channel. Opposite-polarity channels do not cancel visually.
- **CHANNEL 1** / **ACN 0**: only the first file channel, with no automatic switching.
- **ALL CHANNELS**: one row per source channel inside the same waveform window:
  stereo 2, quad 4, octo 8, ambisonics 4/9/16. Discrete labels start at CH 1;
  ambisonic labels start at ACN 0. Rows share the same amplitude scale and timeline.

The selection also applies while recording and reviewing a resample. Trim,
slice markers, zoom and cursors remain channel-linked; this is display only,
not a channel solo, downmix or processing change. The small overview always
shows Combined Peaks. Like zoom, this is an instance-local view preference,
retained when reopening the editor but not stored in a set/project. Sound
state and parameter IDs are unchanged.

Verified 0.22.1 on macOS: 13 focused suites passed, including real GUI switching
with 1/2/4/8/9/16-channel decoded fixtures and live resampling. CLAP validator:
18 passed, 0 failed, 3 skipped. Utility tests now cover every attack velocity
1–127 in both routes and varied hit velocities through Tracker. The installed
three-plugin chain passed 1,527 checks; physical pad velocity behavior still
requires a hardware check. The replaced 0.22.0 bundle is retained in
`~/Library/Audio/Plug-Ins/CLAP Backups/sample-neon-0221.t5a5dU/`.

Version 0.22.0 adds the private control input from **s3g Utility Neon MIDI**,
allowing the same-track chain **Utility Neon MIDI → Tracker 0.4.1 → Sample Neon**.
Primary SAMPLE pads become recordable notes; editing controls pass separately,
with no duplicate primary pad trigger or hardware MIDI return. The Utility is
the performance-bank authority; CHOP keeps its separate slice bank. See the
[Utility setup](../clap_utility_neon_midi/README.md). State/parameters are unchanged.

Version 0.21.1 standardizes every right-hand page on a 28-pixel row-center grid:
PLAY, CHOP, RESAMPLE, Source/Trim, all playback settings and Character FX.
Menus and sliders share label/control columns; action buttons use equal widths,
20-pixel heights and 8-pixel gaps. Menu fields remain 15 pixels high with
18-pixel popup rows. Waveforms, the miniature NEON, sound and state format are
unchanged. See [Tracker pairing](TRACKER_INTEGRATION.md) for current routing
and recommended next development work; that work is not part of this GUI update.

No controller firmware replacement is required. The plug-in receives the
Neon's native MIDI, performs the bank/mode interpretation inside the audio
process, and sends LED/status MIDI through its CLAP MIDI output on non-macOS
hosts. **Since 0.20.1, macOS uses only the direct CoreMIDI LED
connection owned by the active instance, and exposes no CLAP MIDI output.**
Remove REAPER MIDI hardware sends back to NEON: LED messages share addresses
with pad presses and must not be duplicated or routed back into the instrument.
Versions 0.20.1 and 0.21 are state-compatible with 0.20.0; saved cells and fades are unchanged.

In 0.21 each secondary page uses its own large-pad palette index: PLAY FX 16,
CHOP tools 32, EDIT tools/FX 64, RESAMPLE cells 80. Selected tools and sounding
cells use white (127); unavailable tools and empty cells are dark, except the
chosen empty capture destination. Primary cell feedback remains loaded red,
last-played yellow, sounding white. These are hardware colors only; the GUI stays
grayscale. Distinct MIDI values are tested; exact hue appearance still needs
visual confirmation on the connected NEON firmware.

LED recovery on Mac now sends three short pad-only retries after a surface
change, then stays quiet until an LED value changes. It no longer continuously
refreshes unchanged pads every 250 ms. Read-only endpoint discovery still handles
unplug/reconnect. Incoming raw small-status-lamp notes are ignored while NEON
OWNER is on; legitimate pad velocities and simultaneous holds remain supported.

## Four jobs, one set of cells

PLAY is the performance surface. The other modes operate on those same cells,
not three additional instruments or competing slice engines. Selecting an
editing mode preserves the selected cell. Cell and CHOP bank positions are
remembered separately. Ordinary MIDI notes always play the cells, regardless
of the displayed editing page.

| Hardware button | Primary layer | Secondary layer |
| --- | --- | --- |
| Sampler / **PLAY** | Perform 32 cells across banks A–D | Momentary velocity/pressure Pad FX on the selected cell |
| Slicer / **CHOP** | Audition up to 32 slices of the selected source | Marker tools and assignment to empty cells |
| Hot Cue / **EDIT** | Select and play the cell with its current processing | Source editing, Sample/Motion/Grains selection, One Shot/Hold/Toggle and momentary Stop |
| Hot Loop / **RESAMPLE** | Record an internal output bus | Play loaded cells; empty pads choose a capture destination |

Shift is fine/alternate editing, not a secondary-layer switch. Use the Neon's
secondary mode messages, or the miniature GUI's SECOND/PRIMARY button. Clicking
the already selected GUI mode also switches its layer.

### Copying cells

Click a cell pad to focus the editor, then **Ctrl-C / Ctrl-V** to copy/paste
(**Cmd-C / Cmd-V** also work on macOS). Right-click a cell for **Copy Cell** /
**Paste Cell**; Control-click works on macOS too. This is available on the
PLAY/EDIT primary cell pads and the RESAMPLE secondary pads, not CHOP slice/tool pads.
Change bank before pasting to duplicate across A–D. A populated destination
requires **Paste / Replace** confirmation; Cancel changes nothing.

Copies include audio, trim, slices, playback technique and envelope, FX,
source format and output routing. The source is kept, edits are independent,
and paste does not audition automatically. Immutable audio is shared to avoid
duplicating RAM or embedded state; cropped audio remains cropped. The clipboard
is private to this plug-in instance, is not saved with the project, and survives
clearing its source or Reset All until replaced or the instance is closed.
Imported audio remains file-linked. Text/numeric fields retain normal text
copy/paste when focused; click a cell to return to pad shortcuts.

The waveform, overview, trim handles, edit cursor and live voice playheads
remain the main visual workspace. PLAY does not allow accidental trim edits.
Controls occupy bars outside the waveform. CHOP offers **Live Markers**,
**Transients**, **Equal** and **Beat Grid**. Its banks address slices 1–8,
9–16, 17–24 and 25–32. Assign One/All commits each slice as a **new cropped
sample**, with Start/End reset to 0–100%, a one-slice map and fitted waveform.
Select its pad in PLAY or EDIT, then open CHOP to edit or re-slice only that
cell's audio. All channels use the same integer-frame cut; audio outside the
slice is not retained in that cell. **START PAD / TARGET PAD** determines the
destination as described above. Other occupied cells are never overwritten.
The whole assignment is checked for sufficient space before
anything changes; the new audio is also prepared before replacing cells.
Hardware **Shift + CHOP secondary pad 8** explicitly assigns
with replacement, retaining the source-first / remaining-empty-pads shortcut
regardless of the chosen start pad; in stack mode it replaces the source stack.
Unshifted Assign One/All follows the destination menu (initially Auto / Empty).
Replacement changes the source cell's
audio, never the original file on disk. Previously assigned slices in older
sets retain their old trim-based references; reassign them to create cropped samples.
Assigned slices retain the source's tuning, envelope, filter, character and
output route, while starting as ordinary forward one-shots without generative
stages or repeat enabled.

In **CHOP → TRANSIENTS**, **PRE-ROLL** shifts detected slice starts earlier by
0–50 ms (default 0). Drag the slider or click its value to type milliseconds.
The map rebuilds from the original detections, so returning to zero restores
their positions. The source trim edges stay fixed; starts pushed before the
trim are merged into the first slice. Zero-crossing snapping is applied after
pre-roll, so switch it off for exact offsets. The setting and requested slice
limit are saved per source; changing pre-roll never permanently reduces that
limit when early detections merge.

Scroll vertically over the main waveform to zoom **1x–32x**, anchored at the
mouse pointer. **Shift-scroll** or a horizontal trackpad gesture pans the view.
This works in PLAY, CHOP, EDIT and capture review and does not move the edit
cursor, trim or slices. Hardware TRAX zoom still focuses the edit cursor.
Zoom and pan are view-only, not saved sound settings.

EDIT pads now audition without an on-screen button. Primary pads select and
play; secondary pads apply their change/page selection and retrigger the
selected cell through its selected playback technique and
output route. Releases follow the original gesture across page/bank changes.
There are no per-cell mute/solo masks or adjacent-cell mixing. Source editing
never changes the technique. Encoder pushes on Motion/Grains audition the
technique without toggling it off. Retap after changing Shot Length; that
duration is captured when a new gesture starts.

Miniature bank buttons show a loaded-cell count. Pads use an **AUDIO** label,
filled presence marker and brighter background for loaded cells; empty cells
are dim, outlined and labelled **EMPTY**. Linked audio that is still loading
or cannot be found is marked **LOADING** or **OFFLINE**. Playback gets a separate
level strip; selection outlines do not imply that audio is present.

### Zero-crossing edits

**ZERO CROSS** defaults to ON per cell, with a separate capture-review setting.
Trim and slice edits search within ±5 ms (capped at 2048 samples), constrained
by adjacent markers. A crossing shared by every channel is preferred; the
nearest shared crossing wins. If none exists, the least-energy boundary is
chosen by minimizing the worst channel's endpoint energy, then total energy,
then distance. Opposite-polarity channels cannot cancel the score. All channels
always use the same boundary, including ACN/SN3D material.

Turning it OFF permits unsnapped placement; turning it back ON resolves current
boundaries. It never independently shifts channels. DC-offset or difficult
multichannel material may have no real shared crossing in the search range:
the fallback is a best local choice, not a guarantee of silence or click-free
loops. Existing envelope controls remain useful.

On macOS, enable **NEON OWNER** and wait for the button to report
**NEON: CONNECTED**. The owner instance finds the physical Neon and sends the
four-deck initialization plus differential pad feedback directly, without a
REAPER MIDI hardware send. On non-macOS platforms, the CLAP MIDI-output path is the
portable fallback. Feedback addresses both the large performance-pad surfaces
and the five small status LEDs beneath each pad.

## Slot and performance map

The four Neon banks address 32 independent sample slots:

| Neon bank | Slots | Default MIDI notes |
| --- | --- | --- |
| A | A1–A8 / 1–8 | 36–43 |
| B | B1–B8 / 9–16 | 44–51 |
| C | C1–C8 / 17–24 | 52–59 |
| D | D1–D8 / 25–32 | 60–67 |

Secondary pad order is left-to-right across the top row, then the bottom row:

- **PLAY:** Filter, Echo, Comb, Ring, Flutter, Pulse, Drive, Crush.
- **CHOP:** Add, Delete, Audition, Zero Cross, Equal, Transients, Assign One, Assign All.
- **EDIT:** Source, Sample, Motion, Grains, One Shot, Hold, Toggle, Stop.
  In **EDIT VIEW → Character FX**, secondary pads instead select the eight FX.
- **RESAMPLE primary:** Record, Stop, Audition, Discard, Bus −, Bus +, Review, Assign.
  Secondary pads play loaded cells or select empty targets in the current cell bank.

In PLAY, Mode+pad selects One Shot/Toggle/Hold; Slip or Repeat+pad toggles
repeat, and Sync+pad toggles tempo sync. The five small LEDs keep their physical
**One Shot / Toggle / Hold / Loop / Sync** order: loop is the fourth lamp.
Censor reverses new direct-sample gestures while held. Generated techniques
use their own path/order/shape controls. Releases remain attached to the
original voice even if the page or bank changes during a hold.

| Page | LOOP turn / push | TRAX turn / push |
| --- | --- | --- |
| PLAY | Selected-cell gain / no trim edit | Global Mangle / no trim edit |
| CHOP | Cursor / add marker; Shift+push deletes | Zoom / reset; Shift+push focuses slice |
| EDIT Source/Trim | Cursor / set Start; Shift+push sets End | Zoom / reset; Shift+push focuses trim |
| EDIT Motion settings | Scan offset / audition | Cycle (seconds or beats) / no trim edit |
| EDIT Grains settings | Grain size / audition | Density (Hz) or host interval / no trim edit |
| EDIT Slice Sequence | Step rate / audition | Repeats / no trim edit |
| EDIT Stretch | Duration / audition | Tune / no trim edit |
| EDIT Wavesets | Group cycles / audition | Repeats / no trim edit |
| EDIT Character FX | Parameter 1 or 3 / audition | Parameter 2 or Amount / switch pair |
| RESAMPLE | Review cursor / Start; Shift+push End | Zoom / reset |

EDIT encoders follow the right toolbox's Source/Trim or playback view, on either pad layer.
Shift gives fine cursor/continuous-parameter steps. Both 1/127 and 65/63
relative encoder streams are accepted; waveform zoom spans 1x–32x.

## Source normalization

**EDIT → Source / Trim → NORMALIZE SOURCE / -1 dBFS** peak-normalizes the entire
selected source, before cell gain, envelopes and effects. Every channel uses the
same positive scalar, preserving stereo/quad/octo relationships and ACN/SN3D
channel balance; it does not change ambisonic normalization conventions.
Trim, slice markers, playback technique, routing and gain settings remain as-is.
The normalized audio is embedded in the set/project; the original file and other
copied cells remain unchanged. Current playback of that cell stops on commit.
Silence is left unchanged, repeated normalization does not allocate another copy,
and the embedded-audio budget is checked before changing the cell. This is sample
peak normalization, not loudness matching or an output limiter.

## Resampling

Record the selected internal bus **after** cell processing and master gain.
Use **PLAY PADS** on the miniature NEON (the RESAMPLE secondary layer) for banks
A–D while recording; the duplicate toolbox grid has been removed. The bank
buttons show the number of loaded cells. A warning identifies a selected
cell routed to a different bus from the recorder; routing is never changed
automatically. All six playback techniques, One Shot/Hold/Toggle and
multichannel behavior applies. Playing a source never changes the capture target.

On hardware, RESAMPLE primary pads keep their recording tools. Its secondary
layer plays loaded cells and uses empty pads to choose a destination (and audition
a completed take). Releases stay attached to the original cell across bank/page
changes, so Hold does not latch accidentally. The miniature NEON follows this
same mapping. The mini layer button is labeled **PLAY PADS / REC TOOLS** here.
Record/Stop remain available in the toolbox on either layer. Hardware
**SHIFT + pad 1 (REC)** toggles recording on both RESAMPLE layers without playing
pad 1 or changing the capture destination. Pressure/release do not toggle it.
If a completed take is present, the shortcut leaves it intact; explicitly
Discard before recording another take.

Stop is also available on the miniature controller while recording, regardless
of the page. Both waveform views draw **during recording**, with a moving write
cursor and stacked CH/ACN rows for multichannel captures. The first second fills
left-to-right; longer takes fit the growing recording. Display peaks are published
through a bounded atomic buffer, not read from unfinished audio storage.
Stopping hands
off a linked-channel asset for audition, trim and explicit assignment to an
empty cell. Review playback goes to bus 1 of a matching layout and is excluded
from recording. Changing the global layout stops the take. Discard the review
before starting another take; already assigned cells are kept.

Set Start/End, then use **CROP TO SELECTION / DISCARD OUTSIDE** to remove unused
audio from the review take. Hardware **Shift + RESAMPLE primary pad 7** performs
the same crop. It uses identical integer-frame bounds across all channels,
resets the retained take to 0–100%, resets zoom, and auditions the result.
Already assigned cells retain their original immutable audio. Secondary target
pads and the Review action also audition the capture. Cropping is destructive
for the current review, so save a set first if the untrimmed take is needed.

The recorder preserves the bus width and order, including all 16 ACN/SN3D
channels. It records internal audio only, not REAPER inputs or external effects.
Capture is block-accurate, preallocated and limited to 30 seconds or 1,440,000
frames, whichever is smaller (15 seconds at 96 kHz). Finalization and assignment
run on the main thread, not the audio callback.

## Starting over

**RESET ALL** in the header opens a confirmation with **CANCEL / CLEAR ALL**.
Confirming stops playback/recording, clears all 32 cells and the review take,
and resets sound parameters, slice maps, stages and editing positions. The
output layout, base MIDI note, receive channel and Neon ownership are retained.
Imported files are never deleted. Save a set before clearing if you want to
recover the work. Reset waits for the audio callback to stop before clearing
assets, and invalidates pending file loads so cleared cells cannot refill later.

## One playback technique per cell

The **PLAYBACK** menu in the right toolbox is the authority. Its choices are
**Sample**, **Motion**, **Grains**, **Slice Sequence**, **Stretch**, and **Wavesets**;
choosing one commits to it and replaces the previous technique. EDIT pads 2–4
remain fast Sample/Motion/Grains shortcuts; the menu exposes all six.
**EDIT VIEW → Source / Trim** (or secondary pad 1) changes only what is being
edited, never the active technique.

- **Sample:** ordinary playback of this cell, using its trim, direction and
  envelope. With Repeat off, One Shot lasts until the selected sample window ends.
- **Motion:** a continuous scan through this cell using overlapping 40 ms
  windows. Forward, Reverse, Bounce and Wander paths determine the scan;
  Offset selects its initial phase and Cycle sets its speed. This is audible
  motion while a pad sounds, not just a moving next-launch marker.
- **Grains:** a cloud around Position within this cell. Density/Interval,
  Grain Size, Spray, pitch spread and reverse probability shape its texture.
  It does not silently enable Motion or mix other cells.
- **Slice Sequence:** steps through this cell's CHOP markers in Forward,
  Reverse or Random order. Step rate/beat interval, 1–8 Repeats and Chance
  shape the phrase. One Shot uses Shot Length; Hold/Toggle sustain it.
  A committed slice has one marker region until re-chopped; it does not
  sequence neighboring pad cells automatically.
- **Stretch:** overlapping windows traverse the trimmed sample over Duration,
  independently of Tune. This is deliberately textured granular stretching,
  not a transparent phase-vocoder. One Shot traverses once; Hold/Toggle wrap.
  Forward/Reverse controls traversal. Duration uses seconds or host beats;
  there is no competing Shot Length control.
- **Wavesets:** cycle-based rearrangement with Group, Repeats, Shape and Process.
  Twelve shapes include repetition, omission, replacement, interpolation,
  harmonic/fractal processing and cycle/group reversal. PRIMARY One Shot ends
  with the sample; Hold/Toggle loop. PRIMARY is free-running; STACK SCAN adds
  Free/Host, Shot Length and gesture envelopes. Both are disabled for
  Ambisonics. Analysis runs on the file worker; silent/DC sources may lack
  usable cycles. It supports discrete mono/stereo/quad/octo channel order.

Motion and Grains each use the cell's **CLOCK** menu:

- **FREE / PAD** (default): independent of REAPER transport and tempo.
  Motion restarts its seconds-based scan with each strike; Grains uses Hz.
- **HOST TRANSPORT:** Motion follows host beat position, and grain emission
  uses a beat interval at host tempo. Playback pauses while transport is
  stopped; select Free to continue independently. Shot Length counts running
  time, excluding transport pauses.

Slice Sequence and Stretch also support Free/Host clocks; their durations count
running time. Character FX tails can outlast a playback gesture. Explicit Stop
and Kill clear the effect history as well as playback.

Their **TRIGGER** menu has three clear behaviors:

- **ONE SHOT:** a 0.05–30 second **SHOT LEN**, captured at the strike.
  This is the total playback duration, including attack and release, not
  source length or grain length. Long or pitched-down grains cannot extend it.
- **HOLD:** starts with Attack, sustains while held, then fades over Release.
- **TOGGLE:** first strike starts with Attack, second starts the release fade.

Motion/Grains **ATTACK** and **RELEASE** are 0.001–10 seconds per cell (default
0.005). They shape the whole scan/cloud, independently of individual grain
windows. Find them in Playback Settings or Source / Trim. Releasing during
attack fades from the current level, without jumping to full volume. Grains
continue being generated throughout the release, including when the host stops.
For One Shot, the fades fit inside Shot Length; if their sum is longer, both
are proportionally shortened. Fade times are captured at each strike. Explicit
Stop/Kill still silence immediately. These times are shared by Motion, Grains,
Stretch and scanning Wavesets. Sample retains its proportional sample-window
envelope; Sequence retains short technique-controlled fades. Post-playback FX may have tails.

Click slider readouts to enter precise Shot Length, Cycle or grain values.
**SIZE** is an individual grain's source window; it never sets cloud duration.
Motion/Grains show their gesture envelope instead of Sample's proportional
Attack/Release; Sample's Direction is hidden while editing their source.
The Stop shortcut chokes only the selected cell; it never latches a mute.

CHOP still auditions authored slices directly. Sample Repeat/Direction do not
override a generated technique. For Motion/Grains/Sequence/Stretch, hardware Sync+pad toggles
Free/Host; Repeat/Slip+pad toggles One Shot/Toggle. The fourth loop lamp denotes
sustained Hold/Toggle generation; the fifth Sync lamp denotes Host clock.
Wavesets PRIMARY is always Free. Wavesets STACK SCAN also supports Sync+pad
and the Host-clock lamp.

## Character FX: playback → effect → output

Character is now a real post-playback processor, shared by every playback
technique, rather than an alternative set of trigger parameters. Reverse is
kept in playback and Hold/Toggle in Trigger; neither is duplicated as a Character.

Open **EDIT VIEW → Character FX**, or **EDIT CHARACTER FX** from PLAY. Each cell
retains three settings for each of the eight effects; switching effects recalls
those settings. FX Amount blends dry/wet; pressure and global Mangle add to it.
Amount changes are smoothed. One Character runs per cell, not an eight-effect chain.

| Effect / pad | Parameters | ACN/SN3D |
| --- | --- | --- |
| Filter / 1 | Low/High/Band Pass, Cutoff, Resonance | Enabled |
| Echo / 2 | Time, Feedback, Damping | Enabled |
| Comb / 3 | Tuning, Feedback, Damping | Enabled |
| Ring / 4 | Frequency, Wave, Depth | Enabled |
| Flutter / 5 | Rate, Depth, Irregularity | Enabled |
| Pulse / 6 | Rate, Duty, Edge | Enabled |
| Drive / 7 | Drive, Bias, Tone | Disabled |
| Crush / 8 | Bits, Rate, Jitter | Disabled |

The first six use identical linear processing, timing and modulation across
channels. Drive/Crush are nonlinear per component and can alter ambisonic spatial
relationships, so their menu rows and pad shortcuts are disabled for ACN/SN3D.
Selecting that format clears an unsafe Character or Wavesets choice. State recall,
parameter input and DSP also enforce the restriction. There is no implicit decode,
encode or normalization conversion. Discrete quad/octo permit all eight effects.

On the FX page, LOOP/TRAX edit parameters 1/2. Push TRAX to switch to parameter
3/Amount, or use the Encoder Pair menu. Push LOOP to audition. Secondary pads
choose effects and apply momentary pressure amount; primary pads still play cells.
Click numeric values to enter Hz, milliseconds, bits or percentages. RESET THIS
EFFECT resets its three settings without changing playback or other effects.

## Loading and saving

Drop one or more supported audio files onto the active bank's pads. A multi-file
drop fills consecutive slots from the target pad, including following banks.
Loading and decoding happen on a worker thread, so the audio process is not
blocked. Each slot stores gain, pan, tune, Start/End, envelope, filter,
Mangle, pressure depth, character, trigger mode, repeat/sync/velocity, choke
group, shared chop layout, source format and output-bus settings. Playback
technique, clock, Shot Length and all technique settings are saved as well.
Version 0.20 uses schema 14, retaining the plug-in ID and existing host parameter
IDs. New per-effect and playback settings are saved in sets/project state.
Newly saved sets require 0.20 or later. Version 0.19 loads unchanged with the
previous 5 ms gesture fades. Versions 0.13–0.18 still load, but their
old Character meanings are **not sound-compatible**: Character resets to Filter,
cell/global Mangle reset to zero, and the new effect settings use defaults.
Save a separate copy before migrating. Older combined stages resolve to Grains,
then Motion, then Sample. Adjacent-cell mixing and mute/solo remain retired.
Pre-0.16 techniques default to Free clock and a one-second shot.
Versions before 0.13 remain unsupported.
The old stereo-only plug-in ID is no longer provided.

In LINK mode imported files remain linked paths; keep them at stable locations. Committed
slices, captured audio, cells derived from captures, and the completed unassigned
review take are embedded in host state and `.s3gneon` sets. Committed slices
do not need the original file to reload. Shared captured assets are
stored once. State limits unique embedded PCM to 256 MiB; saving fails rather
than writing an unloadable larger set. Stop an active take before saving it.

See the storage menu above for PROJECT collection and explicit EMBED behavior.

## Build

```sh
cmake --preset clap-release
cmake --build build-clap-release --target s3g_sample_neon_clap --parallel 4
ctest --test-dir build-clap-release -R s3g_sample_neon --output-on-failure
```

The macOS bundle is written to:

```text
build-clap-release/plugins/clap_sample_neon/s3g_sample_neon.clap
```

For the REAPER MIDI and hardware-output configuration, see the
[Reloop Neon controller guide](../../controllers/reloop_neon/README.md).
