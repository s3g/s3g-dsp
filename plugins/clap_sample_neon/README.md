# Sample Neon 32

`s3g Sample Neon 32` is a 32-slot Sample-family instrument designed around the
factory MIDI map of the Reloop Neon. Version 0.41.2 exposes one CLAP instrument,
`s3g Sample Neon 32`, with a fixed 32-channel output port. The default Stereo layout
mixes cells onto channels 1–2; unused channels remain silent.

For the current workflow and control reference, see the
[Sample Neon 32 user guide](../../docs/sample-neon.html). The notes below also
retain the implementation history of earlier versions.

GUI buttons and mini-pads briefly flash light gray when pressed (180 ms), then
return to their normal active/loaded/playback indication. This is visual
acknowledgment only; MIDI mapping, held gestures and saved state are unchanged.

## Chromatic pads and independent voices (0.41.0)

0.41.2 pairs with Utility Neon MIDI 0.5.1 to isolate a Keyboard unit's A–D
range from the editor. On primary SAMPLE, range changes update only that
unit's keyboard LEDs and subsequent pitches; the selected sample, software
bank and performance page remain unchanged. The other unit retains normal
Pad Cells navigation. Explicit edit-page presses still work. Tracker and
saved parameter/state formats are unchanged.

0.41.1 receives Utility Neon MIDI 0.5's resolved Scale/Manual Keyboard maps for
root/playing/OFF LED feedback and modifier auditions. It does not overwrite the
pad-note map or change the pinned channel destination. Direct-controller
Keyboard remains chromatic; scale/manual layouts are configured in Utility.

In EDIT VIEW → ROUTING, choose NOTES / VOICES. Each MIDI channel can use the
existing PAD NOTE MAP, address one pinned CHROMATIC A1–D8 cell, or be OFF. Keep
MIDI IN at OMNI to use several channel destinations together. Incoming note
numbers select pitch only on chromatic routes; ordinary pad notes keep their
existing fixed-tuning behavior. ROOT NOTE sets the untransposed key; pad Tune
is added. Total transposition is bounded to ±60 semitones.

VOICES offers PAD DEFAULT, MONO, POLY and LEGATO. Pad Default preserves each
method's previous pad behavior but uses Poly for the new chromatic input.
Explicit Poly also layers repeated ordinary pad hits. Legato uses last-held-note
priority and returns to the previous held pitch without restarting its gesture.
The limit is 1–16 gestures per pad, sharing 32 prepared gesture renderers across
the instrument. Released voices are stolen first, then oldest voices. Internal
grains/windows are separate from this note-gesture count. Character FX process
the sum per pad; stealing a note does not steal that pad's delay tail.

All nine methods have independent chromatic gestures, envelopes and free-running
stack/source state. HOST clocks still deliberately align with transport.
Grains transposes grain pitch without speeding source scan; Motion transposes
the read windows and source traversal, and Lanes links pitch with read speed.
Sample and Wavesets have rate-linked pitch; Stretch, Spectral and Slice Sequence
retain their configured traversal/step timing. Cutups retains its cut scheduler.

DIRECT NEON → PAD ROLE pins a controller's primary SAMPLE pads to one cell.
FIRST NOTE is bank A/pad 1; A–D cover 32 consecutive keys. Selection/edit focus
does not retarget it. Other pages and SHIFT/modifiers keep their existing roles.
Two units have independent pin/range settings. Through Utility/Tracker, use
Utility 0.4's KEYBOARD role and output channel instead; pin that channel in
Sample Neon and set Tracker's recording lane to the same channel. The Utility
role is not a general keyboard-input MIDI thru.

Saved state v27 appends voice/routing/Keyboard settings; older projects retain
their previous behavior. Root/voice settings follow cell copy/paste and the
destructive-edit history. Mapping changes never retarget a held note-off.

## Consistent playback vocabulary (0.40.7)

- **STACK PATH** names automatic breakpoint movement between layers everywhere:
  Layer Src, Lanes Navigation, the Edit View menu, graph and status. Lanes offers
  MANUAL / STACK PATH. **STACK CYCLE** is that path's timing in both inspectors.
- **SOURCE PATH / SOURCE CYCLE** describe movement within a sample, distinct
  from movement between layers. Motion and Grains share these labels; Spectral
  and Wavesets Oscillator also use SOURCE CYCLE for their source traversal.
- **EDIT LAYER** replaces Selected Layer in Layer Src; **PRIMARY LAYER** always
  means layer 1. Edit Layer selection and the sounding layer remain independent.
- **CUSTOM** is the hand-edited breakpoint shape. **MANUAL** in Lanes Navigation
  means direct layer control; choosing Custom does not disable Stack Path.
- Grains' random pitch amount is **PITCH SPRAY**, distinct from GRAIN TUNE.
  Grains/Cutups reversal probability is **REV CHANCE**, not a direction switch.

Stack Lanes remains a waveform view, not the Lanes playback method. Grain SRC
Scan and oscillator Scan still describe within-source movement, so they are
not renamed Stack Path. Cutups' Host Tempo clock is intentionally distinct from
transport-gated Host Transport. CLEAR LAYER keeps layer positions; REMOVE
compacts the stack. These different behaviors retain distinct names.

Shared display labels and regression checks keep the terminology aligned.
No menu indices, saved values, parameter IDs, DSP, MIDI or GUI geometry changed.

## Paced hardware mode feedback (0.40.6)

On macOS, NEON OWNER sends each bank's mode command in its own MIDI packet,
with a 20 ms quiet interval before and after it. These waits run exclusively
on each unit's MIDI-output worker, never the audio thread. Ordinary bank/pad
updates remain batched and immediate; settling retries remain pad-only. A
failed send does not commit the pending LED diff. Turning OWNER off, or closing
the plugin, clears pads without sending a bank or mode selection.

An isolated test with OWNER off confirmed primary HOT CUE on banks A and B:
both bank recalls and pad-note addresses remained HOT CUE after paced writes.
This informs the fix, but is not full plugin/REAPER acceptance of every mode,
secondary layer, bank or two-unit interaction. Reload the updated plugin,
enable OWNER, and verify bank changes on the connected units. Utility stays at
0.3.2; the software protections below, state format and VSTGUI editor are unchanged.

## Bank-wide mode input fix (0.40.5; Utility 0.3.2)

Bank buttons change only the bank, not the selected performance mode/layer.
Both plugins now interpret pads using the explicitly selected page, even when
a deck recalls old pad addresses. Velocity is paired using the original raw
address first; pressure and releases remain attached to the original hold.
USB's same-packet recall filter is retained. Host MIDI also filters the captured
adjacent bank + mode-on/off triplet at the same timestamp and port; later real
mode presses are not time-suppressed. CHOP accepts empty banks without changing
page or auditioning nonexistent slices. Two USB units remain independent.

This enforces the software rule rather than assuming mode-LED writes update
firmware memory. 0.40.4's output-only change did not pass hardware acceptance.
See 0.40.6 above for paced output and the scoped hardware test. Keep NEON OWNER
enabled for feedback. In a Utility chain, select
performance modes with the hardware buttons; the downstream GUI is not a
return connection to Utility's upstream note mapper.

## Bank-wide feedback and fit-all Stack Lanes (0.40.4)

Selecting PLAY, CHOP, STACK or RESAMPLE now sends the chosen mode and primary/
secondary layer to all four remembered hardware decks before restoring the
selected bank lamps and pad colors. Ordinary bank changes and pad-settling
retries do not repeat page commands. The behavior is per physical USB unit,
so two NEONs can still use different modes and banks. Sample Neon must own
hardware feedback. Actual hardware acceptance still needs a host session;
tests verify the outgoing protocol and independent contexts, not firmware
mode switching. See the 0.40.5 input fix above.

Stack Lanes displays all 1–32 layers in the fixed waveform area: L01 at the
top, highest layer at the bottom. Rows scale to fit, with compact gaps/labels
and clipped cursors; playback no longer scrolls an eight-lane window. The
header shows ALL and the layer count. Channel modes, audio, state format,
VSTGUI backend, plugin ID and parameter IDs are unchanged.

## Pad/layer clipboard and clear (0.40.3)

Right-click a PLAY cell (or RESAMPLE secondary cell) for COPY CELL, PASTE CELL
and CLEAR CELL. Clear removes all of that pad's audio without changing its
playback/FX/routing or deleting files from disk. Right-click a Source / Stack
grid position or miniature STACK pad for COPY LAYER, PASTE LAYER and CLEAR LAYER.
Empty positions, including L32, accept paste; occupied targets require confirmation.
Clear Layer keeps other layer indices fixed (unlike the compacting REMOVE button).
If the selected trailing layer is cleared, editing moves to the last remaining
position so the saved state remains valid.

The separate layer clipboard retains immutable PCM, file linkage, trim, slices,
cursor, BPM and source analysis, not pad playback/FX/output routing. Nonempty
stacks must match channel width and source format. A sole/new source adopts
the copied format, including normal ACN/SN3D safety clamps. Ctrl/Cmd-C and V
follow the focused grid position; click a cell pad to restore cell shortcuts.
Both clipboards are session-local and survive Clear and Reset All. Paste/Clear
are undoable checkpoints and do not audition. Clearing a recorded layer retains
its review and invalidates its deletion link; Undo restores both source and link.
Stable plugin/parameter IDs, VSTGUI, and state version 26 remain unchanged.

## Retained recording review (0.40.2)

Pad Stack takes now stay in the RESAMPLE waveform after Stop, sharing the
immutable PCM of the saved layer. The latest completed take is shown with its
pad/layer destination. Audition plays it; Stop ends audition without clearing
the waveform. The live peaks remain visible while the final take publishes.

**DELETE TAKE** in the waveform toolbar/toolbox (primary RESAMPLE pad 4) deletes
only that recorded layer and its review, without shifting other layer indices
or deleting files. Selection/record-target changes cannot redirect it. Undo
restores layer, review and deletion link; Redo deletes again. **KEEP LAYER /
CLEAR REVIEW** preserves the layer. Unassigned reviews retain Discard semantics.

Each completed stack take replaces the displayed review in the same Undo step
as layer assignment. Review cropping leaves the saved layer intact. Replacement,
reordering or cropping invalidates the session-only deletion link; so does set
recall. The label then becomes DISCARD and clears only review audio. Review PCM
itself persists through set/project recall using existing state v26. Review
audition requires a matching output layout; it never decodes or folds channels.

## Primary waveform crop (0.40.1)

In PLAY/STACK's **EDIT LAYER** waveform view, **CROP TO SELECTION** beside
Cursor → Start/End destructively retains the current Start–End interval in
only the selected layer. All channels share exact integer-frame bounds.
Trim becomes 0–100% and zoom resets; relative cursor/slice markers, BPM,
playback, FX and routing stay intact. Other layers/pads and source files on disk
are untouched. Generated cropped audio follows the usual storage policy.
The pad stops on commit, with no automatic audition. Undo restores the complete
original layer/audio/settings; Redo reapplies the crop without reading a file.
Recording, pending source loads and pending edits block cropping. Full-length
crop is a no-op and does not consume history. The queued action retains the
requested pad/layer if selection changes before its main-thread callback.

## Live recording and layer takes (0.40)

RESAMPLE now selects **NEON OUTPUT** (post-master internal bus) or **TRACK INPUT**
(incoming REAPER track audio before Neon). A new fixed 32-channel input port
supports mono/stereo/quad/octo and 4/9/16-channel ACN/SN3D, selected independently
of output layout. Input Format is a declaration, not a spatial conversion;
Channels chooses the corresponding input pin group. Width/format must match an
existing destination stack. Float32/float64 inputs are supported.

Choose **RECORD TO → PAD STACK**, Target pad and an empty L01–L32 or **NEXT EMPTY**.
Record latches on press and off on the next press; mouse/pad release does not stop
it. **TAKE / NEXT LAYER** commits and continues into the next empty higher-numbered
layer. Stop commits the last take. Existing/offline/loading layers are
protected; there is no wrap. Every take is a separate destructive-edit checkpoint.
First recording into a sparse empty stack selects the recorded layer for playback.

Hardware in RESAMPLE: primary Record and Shift+pad 1 are toggles; Shift+pad 2 is
Take/Next on either layer. Primary pad 8 becomes Take/Next during stack recording.
The same workflow works with internal resampling, or choose **REVIEW TAKE** for
the existing audition/crop/assign workflow. Review Take mode protects a completed
review; Pad Stack replaces it with the latest completed take, with Undo recovery.

**MONITOR → OFF** is the default. **INPUT THRU** adds selected input pins to the
same-numbered output channels at unity gain after Neon/Fill; no folding/decoding
or master gain is applied. Avoid duplicate monitoring/feedback routes. It does
not affect what is recorded. Feed audio through REAPER's normal FX chain/input
pins; use a separate MIDI route or Utility's direct USB mode when the track's
live input is audio. Host acceptance and actual input routing remain user checks.

Two preallocated recorder buffers switch at audio-block boundaries; publication,
analysis and journal writes remain on the main thread. If both are occupied,
Take leaves the current recording running and asks to retry. Last layer, missing
required input, per-take duration limit, or exhausted set PCM budget stop safely.
Maximum per take remains 30 seconds / 1,440,000 frames; two 16-channel recorder
buffers reserve approximately 176 MiB at 48 kHz. Pending takes must finish before
source replacement, storage changes, undo or recall. Stop before saving.

Source/format/pin-group, target layer, Record To and monitor preference use an
append-only state v26 extension when nondefault; old state formats retain their
existing byte layout. No saved active-record state and no CLAP/parameter-ID changes.
Generated layers use normal storage policies; completed takes use existing Undo.

## Destructive-edit history (0.39)

Persistent header **UNDO / REDO** buttons and a lower action-name readout protect
file loads, layer addition/removal, cell paste, normalization, slice assignment,
recorded takes, capture crop/discard/assignment, and Reset All. A multi-pad slice
assignment or stack normalization is one step. Cmd/Ctrl-Z undoes; Shift-Z with
the same modifier or Ctrl-Y redoes. Native text fields keep text-editing undo.

Each affected pad is restored as a complete audio/settings checkpoint, so later
knob/marker changes on that pad are restored too. Unaffected pads and global
MIDI/output/controller connections stay intact. Knob/menu/marker edits are not
individually journaled. Undo stops voices/fill/audition without retriggering.
Finish recording/loading first. New destructive edits clear Redo.

History holds at most 32 edits / 512 MiB unique PCM references (shared, not
copied), evicting oldest steps as needed; this is not a total plug-in RAM cap.
Oversized individual snapshots reject the edit before mutation. Restores also
respect the active storage budget. Journal management/restoration is on the
main thread, behind the audio callback's reset acknowledgment gate.

History survives editor close/reopen and activation, but is not serialized.
Successful project/set recall clears it; failed/cancelled recall does not.
Load Set explicitly warns that it starts new history. Save Set retains history
and remains the durable checkpoint. Stable CLAP/parameter IDs, state format and
full VSTGUI editor remain unchanged.

## Cutups playback (0.38)

`CUTUPS` uses the actual Sample Cutups scheduler/readers/joins and output
allocator, specialized for a pad's entire 1–32-layer stack. Standalone Cutups
retains its four-lane specialization. Loaded-layer order adapts to 1, 2, 3 or
more sources; empty manual addresses resolve to the nearest loaded layer.

- Layer Order replaces Layer Source for this method. Down/Up, Palindrome,
  Random/Random Cycle, Pairs, Outside In, Center Out, Stagger and Manual retain
  Cutups behavior. Stagger visits successive four-layer groups above four.
- Playback exposes Free 0.1–80 Hz / Host Tempo divisions, Equal/Transient,
  shared Steps/Regions 1–64, Repeat 1–16, source order, Gate, Join, Swing,
  time/reverse/pitch/level variation, Tempo Sync, Seed, Poly/Mono/Legato and
  the original four Poly Path relationships.
- Source analysis runs off audio; each layer keeps its own trim and BPM.
  Layer BPM edits the selected edit layer (20–400 BPM). Tempo Sync is
  varispeed and changes pitch, exactly as in Cutups. Host Tempo derives rate
  from BPM and is not gated by transport play/stop.
- Hold releases the matching voice; One Shot completes one pattern including
  repeats; Toggle stops the pad on its next press. No separate Shot Length.
  Attack/Release are Cutups' whole-note envelope; Join is per cut.
- The fixed overview strip becomes the cut-pattern graph (no waveform resize).
  Click/drag writes manual layer addresses; Option/Alt edits source position.
  Step/Layer/Source controls provide precise editing even in a 32-layer stack.
  Cursors and Follow Stack use actual rendered layers; Stack Lanes remains
  available. STACK position/path performance does not override Cutups ordering.
- Preserve Field is coherent across all ACN/SN3D channels. Discrete Distribute
  uses Neon's bus/width/traversal and Cutups' per-note/cut/pattern allocation.
- EDIT encoders: LOOP = cut rate/division, TRAX = Step Repeat, LOOP push =
  audition. Existing Perform gain/Mangle assignments remain unchanged.

Cutups' standalone root-key transposition/factory presets are not imported:
Neon MIDI notes address pads, shared Tune sets pitch, and pad copy/paste/set
storage retain the whole method. Analysis uses Cutups' default 1 ms preroll;
the CHOP preroll/markers remain a separate editing process. State v25 stores
the new controls/pattern; older states retain their previous playback meaning.

## Custom pad-note maps (0.37)

`ROUTING → PAD NOTE MAP` opens a shared 32-cell numeric editor: A–D columns,
0–127 unique MIDI notes, Copy/Paste List, Default and From A1 sequential fill.
Paste order is A1–A8, B1–B8, C1–C8, D1–D8. Apply commits the complete draft;
Cancel discards it. Applying a changed map stops sounding pads.

Use Utility Neon MIDI 0.3's title-row `PAD NOTE MAP` in a Utility → Tracker →
Neon chain. Neon's `FOLLOW UTILITY` receives its map over a private v3 SysEx
envelope before new performance notes, independent of LED ownership. Choose
`LOCAL` to opt out. Notes Only suppresses all map/control envelopes. Maps save
in project/set state; the received map is retained as a standalone fallback.
Copying pads and Reset All preserve the instrument-wide map. Existing Tracker
patterns are not transposed. Custom maps ignore Base Note; Default restores it.
State v24 appends 34 map bytes to the v23 layout only when needed. Old states,
CLAP IDs, parameter IDs and the VSTGUI editor remain intact.

## Optional second USB NEON (0.30)

Use **Utility Neon MIDI 0.2.0 → Tracker → Sample Neon 32**, with Utility INPUT
set to `USB TWO NEONS` and its route set to `TRACKER + SAMPLE NEON`. Connect
both units by USB without Smart Link, and enable Sample Neon's `NEON OWNER`.
Do not add a MIDI hardware send. Unit 1 starts on A and unit 2 on B; both can
change banks independently. Unit identity is retained before MIDI reaches
Tracker; merging raw NEON inputs cannot provide this distinction.

Each controller has its own page, cell/slice/stack banks, selected cell, modifier
and held-gesture context. Their LED frames go only to their paired USB outputs.
The GUI follows the last press/encoder, identifies U1/U2 beside NEON status,
and does not follow another unit's pressure/releases. Both control the same
samples, parameters, recorder and global fill buffer. Disconnecting a unit
releases its held gestures without resetting the other surface.

Single-controller raw MIDI and the older Utility control bridge remain valid;
no Sample Neon parameter IDs or saved-state layout changed. USB assignment,
swap/relearn controls and both performance-bank settings live in Utility. See
[the Utility setup](../clap_utility_neon_midi/README.md#optional-two-usb-setup-020--sample-neon-0300)
for input modes and ownership. Actual dual-device LEDs, reconnection while
holding a pad, and Tracker recording need testing in REAPER after reload.

Double-click any slider track to restore that control's default. This includes
the inspector's playback, stack, character FX and resample controls, plus the
mini-NEON's contextual TRAX/LOOP sliders and master gain. Only that control is
reset; sources, playback mode and other settings are kept. Trim resets retain
the zero-crossing setting and recalculate transient/beat-grid slices as needed.
Layer Position is a navigation gesture: resetting it also selects Manual and
releases any latched STACK override, just like moving the slider.
This update also keeps active audition/stack sources alive across host audio
reactivation when a take is cropped or a layer is replaced.

## STACK performance and compact controller (0.29)

**HOT CUE → STACK** addresses layers of the selected sample cell. The GUI stays
inside PLAY; the miniature controller switches to clearly labelled layer pads.
Its four buttons are PLAY, CHOP, STACK and RESAMPLE. Banks A–D address layers
1–8, 9–16, 17–24 and 25–32 without selecting a different cell. Sample, slice and
layer banks are remembered independently. Upgrade **Utility Neon MIDI to 0.1.3**
alongside this version so layer-bank messages cannot change Tracker's sample bank.

- Press a layer pad to audition it with the cell's playback method and trigger
  policy. Empty/missing layers are disabled. This does not change Edit Layer,
  Layer Src, playback method or the saved stack path.
- With Motion, Grains, Stretch, scanning Wavesets or Lanes already running,
  holding a layer pad temporarily steers the existing voice to that layer;
  releasing resumes its saved navigation without resetting its clock/envelope.
  The most recently held layer wins; releasing it restores an earlier held layer.
  GUI and hardware holds coexist. Releases retain their original cell and layer
  across bank/page changes. HOST clock still follows transport; FREE remains free.
- **LOOP:** manually navigate the stack. Generated methods glide through layers;
  Sample auditions the nearest loaded layer when that choice changes. **LOOP
  push**, GUI **RESUME**, or double-clicking LOOP releases the manual override.
  Held pads temporarily take priority over manual navigation.
- **TRAX:** 1–100 ms layer transition slew, using the pad's saved Lanes slew
  setting. Grain/window tails retain their normal duration. Shift gives fine
  encoder steps; double-click restores the declared default.
- **Shift + layer pad** selects its edit layer without playing. In the GUI,
  Shift-click or right-click the layer pad does the same.
- Loaded hardware layers use a distinct stack palette; the edit layer is yellow,
  sounding layers white, and empty layers dark. GUI outlines show the edit target
  while the small playback strip follows the sounding layer/blend.
- Slice Sequence and primary-only Wavesets retain their primary-source design;
  STACK performance is disabled there, but Shift/right-click edit selection works.
  Enable STACK PATH for Wavesets layer performance. Ambisonic restrictions remain.
- Secondary HOT CUE editing/FX shortcuts remain available. STACK gestures travel
  through Utility's private control route; Tracker does not record them as notes
  or automation. Primary SAMPLER pads remain the note-recording surface.

The miniature NEON is now anchored directly beneath the persistent PAD toolbox.
TRAX, LOOP and MASTER share its label/track/value columns. All Motion/Grains
controls occupy the Playback inspector; compact family typography
is unchanged. Neither transient cue holds nor manual
override positions are saved in projects/presets or copied with a pad.

## PLAY workspace

The main workspaces are **PLAY**, **CHOP**, and **RESAMPLE**. PLAY includes
all source, playback, stack-path, FX and routing editing; there is no separate
EDIT workspace. Its waveform permits cursor/trim editing whenever it is showing
the edit layer, and its primary pads always perform the cell's trigger mode.

The miniature NEON's **ENCODERS** menu selects **PERFORM** (TRAX = Mangle,
LOOP = cell gain) or **EDIT** (the current Edit View's contextual controls;
Source / Stack = waveform zoom and cursor). This menu changes only knob
assignments, not hardware mode, bank, playback engine or MIDI note addresses.
Leave the physical NEON on Sampler when recording notes into Tracker; selecting
EDIT encoders in the GUI keeps those Sampler notes intact. Hardware Sampler
selects PERFORM; Hot Cue enters STACK performance inside PLAY. Bank changes and primary pad presses
preserve the chosen encoder assignment; workspace buttons select primary pads.
Use SECOND/PRIMARY explicitly for secondary tools. Fill Hold still temporarily
overrides both encoders.

## Persistent pad toolbox (0.28)

**EDIT VIEW → ROUTING → MIDI IN** chooses OMNI (default) or CH 1–16 for
sequencer/keyboard notes across all 32 cells, not just the selected pad. Match
it to Tracker's output channel. The existing Standard MIDI Receive host
parameter is saved/automatable; Reset All retains it. Base Note still determines
the first cell's note (36 by default).

Utility's private NEON controls remain independent of the musical channel.
When an explicit channel is selected, notes on that channel in the cell note
range take musical priority over overlapping raw NEON addresses. For raw
hardware input without Utility, avoid its factory channels 4–12 if simultaneous
sequencing and hardware control would share addresses; use the Utility bridge
to distinguish them. OMNI retains the existing raw-hardware priority and LED
feedback rejection. MIDI and CLAP notes honor the channel selection; pressure
from a different explicit channel is ignored.

The 1504 × 846 (16:9) VSTGUI editor aligns PLAY/CHOP/RESAMPLE and both right-hand
toolboxes at the same top edge. Toolbox labels have 16 px side insets; title-bar
buttons and menus share the standard 15 px height. A right-aligned **PK** readout
shows the actual all-output-channel peak in dBFS, including overloads. Waveform
actions and view menus occupy separate padded rows below the panel header.
The main waveform is always 791 × 510 px; switching engines, inspector pages,
STACK, CHOP, RESAMPLE or FILL never changes its size. The overview/stack path
also stays in the same place. The waveform panel ends level with the full
Grains Playback toolbox (y768), giving Stack Lanes 74 px more vertical room.
The miniature NEON retains its existing near-square pads. All Motion articulation /
event variation and Grains window / process controls live in **EDIT / PLAYBACK**,
in one column with the existing 24 px row pitch and full-width sliders. There
is no separate toolbox beneath the waveform. **CHARACTER FX** is the single
home for Character, FX Amount and Pressure; these are no longer duplicated in
Playback. Contextual panels fit their visible controls. Operation messages
sit 8 px below the content, with a 12 px bottom margin. A separate **PAD** toolbox stays visible on PLAY, CHOP,
RESAMPLE and BUFFER FILL: Playback, Layer Src/Navigation, Edit Layer, Trigger,
Gain, Tune, Audition Pad and Stop Pad. It always targets the selected pad, not
the captured take or global fill. Its Playing readout follows the sounding
layer without changing Edit Layer. Audition Pad plays the processed pad;
**Audition Edit Layer** specifically auditions the edit target.

The PAD toolbox is the single menu/control location for Playback, Layer Src /
Navigation, Edit Layer, Trigger, Gain and Tune. Stack Path no longer repeats
Layer Src, CHOP no longer repeats Edit Layer, and Stretch/Wavesets no longer
repeat Tune. Miniature TRAX/LOOP controls are shortcuts to the hardware's current
assignments, not independent parameters; layer values use layer numbers.

### Navigation ownership (0.29.3)

Selecting Layer Src or Navigation explicitly clears a latched STACK manual
override. For generated playback it also exits an isolated Edit Layer audition,
without retriggering the running voice. Selecting STACK PATH therefore
resumes the saved path immediately; stopped pads still need a trigger.
Moving the LANES Layer Position slider (including numeric entry/default reset),
Previous/Next Layer, or its Loop encoder selects Manual and takes control in the
same way. The slider remains usable while STACK PATH is running.

A physically or onscreen-held layer pad keeps momentary priority until released;
the newly chosen navigation then takes over. PLAY LAYER reports HELD, OVERRIDE,
EDIT AUDITION, the sounding layer/blend, or STOPPED, independently of EDIT LAYER.
The Stack Path status distinguishes inactive navigation, missing stack layers,
stopped/ready pads and a stopped HOST clock. Scan requires at least two layers,
a running pad, and either FREE clock or running host transport.

The contextual **EDIT VIEW** menu lives inside PLAY, in this order:
**SOURCE / STACK**, **PLAYBACK**, **STACK PATH**, **CHARACTER FX**, **ROUTING**.
Source / Stack combines trim and layer management. Routing holds source format,
output bus, applicable pan and pad-velocity response. Playback includes the
method's clock/envelope, Motion articulation and Grains window/process controls
in one column alongside the waveform; there is no nested Detail menu.
Character selection, FX Amount and Pressure live only on Character FX. Physical NEON
secondary tools and contextual EDIT encoder shortcuts retain their meanings.

Playback menu display order is **Sample, Lanes, Motion, Grains, Stretch,
Wavesets, Slice Sequence, Spectral**. Stored engine identities are unchanged. Choosing
an engine updates the sound but preserves the current editing task. Stack Path
returns to Source / Stack if the new method cannot traverse layers. Choosing
an Edit View never selects a different playback engine.

## Playback views, slice envelope and routing (0.32)

- **STACK LANES** is available in every playback method when the cell has
  multiple layers, including Sample, Motion, Grains, Stretch, Wavesets and
  Slice Sequence (which still plays only its primary layer). Since 0.40.4,
  all rows scale to fit the waveform without scrolling. Combined source peaks,
  voice/layer levels and read heads show what is actually sounding.
- **Motion** keeps its locus/field overlay directly on the waveform. The
  separate trajectory scope has been removed, restoring waveform height.
  The full-width editable stack path remains separate and unchanged.
- **Grains** draws each sounding grain's actual source window and envelope.
  Played portions are dimmer, remaining portions brighter, with a moving
  read cursor. Reverse, variable sizes and latched window/skew
  are taken from the voice, including across stack blends. Overlays repeat
  on the existing channel rows without changing multichannel/ACN audio.
- **FOLLOW STACK** and **EDIT LAYER** use the original full-height vertical
  playback lines. FOLLOW STACK retains multichannel channel selection and
  blending; EDIT LAYER retains source trimming. STACK LANES is read-only and
  explicitly shows combined peaks, not individual source channels.

View choices do not change sound or edit-layer selection. Playback data is
published through a bounded, lock-free display snapshot.

**CHOP → SLICE AMP / PROPORTIONAL** sets a shared slice ADSR shape for the
pad. Attack, Decay and Release scale to each slice's pitch-adjusted length;
in Slice Sequence they fit the shorter of that length or the current step.
Sustain is its level. ADR totals above 100% scale together to fit.
Each hit latches its envelope, so edits apply on the next slice. CHOP audition
and Slice Sequence use the same shape; the latter links directly to these
controls. Sequence handoffs use a smooth, complementary overlap of up to 5 ms,
bounded to a quarter step (and shorter for tiny slices). Incoming and outgoing
audio keep their own read heads and routing: no hard cut or frozen-sample tail.
Safety fades remain active at zero Attack/Release, including skipped steps;
a minimum decay ramp prevents a zero-Decay jump from Attack peak to Sustain.
All source channels share the same fade, preserving ACN/SN3D field coherence.
Short slices still finish naturally; the safeguard does not stretch them or
fill deliberate gaps. CHOP audition and the other playback engines are unchanged.

**EDIT VIEW → ROUTING** offers **PRESERVE FIELD** (the default, retaining
source channel order) and **DISTRIBUTE** (fold each event to a MONO or STEREO
object and place it within the selected output bus). Traversal choices are
Sequential, Reverse, Palindrome, Random and Random Cycle. Notes, Motion
windows, grains, slices and Stretch windows allocate per event; continuous
Lanes/Wavesets allocate once per pad gesture. Choose Quad/Octo to distribute
stereo pairs beyond the single pair in a Stereo bus. Changing routing stops
existing voices so old channel assignments cannot bleed into the new mode.

Grains' **STEREO LINK** keeps both channels on one read trajectory or gives
them independent position-spray, pitch-spray and reverse decisions. It is
available for discrete stereo sources; wider fields remain channel-linked.
ACN/SN3D forces Preserve Field and linked reads, with destructive routing
choices disabled. Distribute never implicitly encodes ambisonics.

Stable CLAP/parameter IDs and versions 10–19 remain readable. New routing
or envelope settings use an append-only v20 state tail; untouched older sets
retain their previous state format. CHOP assignments and RESAMPLE are unchanged.

## Buffer Fill Hold (0.27)

**Hold SHIFT + the top-right MODE/CENSOR button** on NEON, or hold the
on-screen **FILL HOLD** button. On primary SAMPLE this is **SHIFT + MODE**;
on other pages it is **SHIFT + CENSOR**. Version 0.30.1 handles both factory
addresses. Utility 0.2.1 also clears the modifier when the release address
changes after switching pages.
This is a global output override, not another pad technique: it grabs recent
heard output, then replaces every output bus with a deconstructed repeat of
that buffer. Pads, Motion/Grains/Lanes and Tracker continue underneath. Release
crossfades back to their current position, not the position where Hold started.
Unshifted CENSOR retains its existing reverse-on-trigger behavior.

- **FILL SET** opens a compact global toolbox. **GRAB** selects the lookback
  on the next press: 1/4, 1/2, 1, 2 or 4 quarter-note beats. The rolling history
  is capped at four seconds; a fresh instance uses whatever history exists.
  With no history yet the press leaves the output dry. The readout shows actual
  available history, or the frozen length while the fill sounds.
- **REPEAT** ranges from quarter notes to 1/128 notes, measured against host
  tempo (120 BPM before a tempo is received). This is immediate performance
  triggering, not next-beat quantization. It also works with transport stopped
  while the host continues processing audio.
- At **BREAKUP = 0**, the latest fragment repeats regularly. Increasing it
  introduces reordered fragments, reverse fragments, smaller ratchets and gaps.
  All choices happen at fragment boundaries; short edge fades avoid hard cuts.
- While held, **LOOP** changes Repeat and **TRAX** changes Breakup (Shift gives
  finer Breakup movement). The encoders temporarily stop editing the pad.
  Their on-screen labels change too. GRAB is next-hold; Repeat and Breakup are
  live controls. Double-click Breakup restores 50%.
- The dry signal is fully replaced after a 5 ms entry crossfade; release uses
  5 ms too. Master gain still controls frozen sound. The second rolling tape
  records the result, so the next grab can include a previous fill.
- All 32 channels share read positions, reversals, gates and envelopes. Bus
  positions and ACN/SN3D channel relationships are retained; there is no remix
  or channel permutation. RESAMPLE records the overridden output, including
  the fill, rather than the hidden live mix.
- Releasing MODE/CENSOR still works if Shift was released first, or the page
  or bank changed.
  **RELEASE FILL** is a GUI escape. KILL, Reset All, host reset/deactivation,
  layout changes and successful state recall clear the tapes; MIDI All Notes
  Off/All Sound Off release the fill. Hiding/destroying the editor releases a
  mouse-held fill, without cancelling a separately held physical button.

Use Utility's control-forwarding mode with Tracker; **Notes Only** intentionally
does not carry this control shortcut. No hardware MIDI return route is needed.
The actual controller mapping and feel still need confirmation on hardware.

History and held state are transient, never embedded or written to sample files.
Only non-default Grab/Repeat/Breakup settings require the appended v19 state
extension; default settings preserve the prior v14/v15/v17/v18 save formats.
Old states load with 1 beat, 1/16 repeat and 50% Breakup. Older binaries cannot
load v19 sets. These are global state-backed performance controls, not per-pad
clipboard values or new host-automation parameters. Two four-second 32-channel
tapes use about 49 MB at 48 kHz, allocated on activation; Hold never copies a
large audio buffer or allocates on the processing thread.

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
- **STACK PATH** optionally moves Layer Pos along the existing shared breakpoints.
  Stack Cycle uses seconds in FREE or beats in HOST. Manual navigation and the
  read speed remain separate from this path's cycle speed. Returning to MANUAL
  restores the saved manual position; touching LOOP takes over from the live
  path position. Selecting a named path shape does not change manual position.
- **HOLD** runs while held (plus Release); **TOGGLE** runs until the next press;
  **ONE SHOT** uses Shot Len. Attack/Release shape the whole gesture. FREE works
  with transport stopped; HOST pauses/resumes with transport. STOP/chokes stop
  immediately. CHOP still auditions the selected source directly.
- On **EDIT VIEW → PLAYBACK**, **LOOP** controls Layer Pos and **TRAX**
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

Use **EDIT VIEW → PLAYBACK** for integrated Motion or Grains settings, and
**STACK PATH** for layer movement. The hardware encoder mappings remain
available for the familiar position, cycle, size and density controls.

- **Stack Path:** select STACK PATH for Motion/Grains/Stretch/Wavesets or LANES.
  The Stack Path SHAPE menu offers
  CUSTOM, RAMP UP, RAMP DOWN, TRIANGLE, SINE, SQUARE and WANDER.
  The graph matches Stack Lanes: layer 1 is at the top, and higher-numbered
  layers run downward. Point editing and the live cursor use that same direction;
  saved values and playback order are unchanged.
  Every choice uses the visible breakpoints for playback; there is no separate
  PRESET path engine. Selecting a named shape regenerates its points and clears
  Curve. Both the path graph and the small waveform overview span the same
  width as the main waveform. Cursor/blend and zoom readouts sit below the
  overview instead of taking a left-hand gutter. The path's moving dot and
  vertical guide show the DSP's path phase, including PHASE offset, Free/Host
  clock and Grains' event-based advance. A hollow dot indicates a STACK pad
  override; the saved path keeps advancing behind that override.
  Click a node or empty space to switch the current shape to CUSTOM without
  resetting its points. Empty-space clicks add a point; drag nodes horizontally
  for time and vertically for layer position. Right-click (or Control-click on
  macOS) removes an interior point. The two endpoints cannot be removed; their
  times stay at 0/100%, but their heights remain editable. Points cannot cross.
  The selected point's TIME and POINT LAYER sliders also edit its coordinates
  once Custom is active. POINT LAYER edits that breakpoint, not the live
  playing-layer position.
  Use 2–32 points; Triangle/Wander need at least 3, Square 4 and Sine 9.
  Changing point count regenerates named shapes or resamples the Custom curve.
  RESET STACK PATH rebuilds the selected shape (a ramp when Custom), keeping the
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
  Around or Ahead. Grain Tune is independent of source scanning. Source Cycle
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

Version 0.25.1 preserves old authored breakpoint paths as Custom. Older
Forward/Reverse/Bounce paths become Ramp Up/Ramp Down/Triangle points; old
Wander is sampled into 32 points (a close approximation, not bit-identical).
Opening the editor alone does not rewrite the sound.

This is a Neon-specific integration, not full binary parity with the three
standalone instruments. Pad Default retains one generated gesture per pad;
0.41's note-voice controls add independent Poly and Legato gestures. Each
gesture has a bounded internal window pool; dense, long grains can voice-steal.
It does not import every standalone routing/randomization option, including independent
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

Each of the 32 pads can hold up to 32 source layers. Open **EDIT VIEW → SOURCE /
STACK** from PLAY. ADD LAYER appends; LOAD replaces the
selected layer. Dropping multiple files while the stack editor is open appends
them to the selected pad. REMOVE LAYER removes only that layer, never its file
on disk. Layer 1 is the primary. All layers in a stack must have the same channel
count; source format and output routing remain pad-wide. ACN/SN3D channels are
never independently selected, reordered, or randomized.

**LAYER SRC** and **PLAYBACK** are separate:

| Source mode | Selection |
| --- | --- |
| PRIMARY LAYER | Layer 1, regardless of the layer displayed for editing |
| EDIT LAYER | The layer selected in the editor |
| VELOCITY | Equal velocity bands across the stack, quiet to loud |
| RANDOM / TRIGGER | One choice per pad strike, held throughout that gesture |
| STACK PATH | Continuous traversal with Motion, Grains, Stretch, or discrete Wavesets |

STACK PATH has its own cycle and breakpoint shape (Custom, ramps, Triangle,
Sine, Square or Wander). Its clock
follows the pad's FREE/HOST choice: FREE runs with transport stopped; HOST
follows the host clock. The existing Motion path or Grains position still
controls time *within* each source. Overlapping, channel-linked grains blend
neighboring stack layers with linear weights; each voice retains its source
until it ends. HOLD release uses the existing gesture release envelope.
One Shot uses the existing Shot Length; Toggle continues until tapped again.
The stack grid shows the current blend without changing the editing selection.
The waveform's **FOLLOW STACK / EDIT LAYER** menu defaults to following the
playing source in every source mode. Primary Layer, Edit Layer, Velocity and
Random / Trigger show the actual triggered layer, including its trimmed waveform,
overview, channel rows and source-matched cursors. Overlapping Sample hits follow
the most recently triggered voice still playing; when it ends, an older surviving
hit becomes visible. The stack grid's playing indicator follows the same source.
During Stack Path, main waveform, overview, channel rows, and source-matched cursors fade
between the neighboring layers using the audible scan weights. Each trimmed
layer is aligned to the same normalized timeline, including different source
lengths. The header identifies both layers and blend percentage. Following never
changes the editing selection; trim/cursor editing is disabled on the followed
display. Choose EDIT LAYER for a fixed, editable waveform. CHOP and RESAMPLE
always keep their own fixed source views. Stopping or isolated audition restores
the selected-layer view. This display choice is instance-local, like channel view.
Start/End, slice map, and source-editing settings are
kept per layer. Gain, technique, envelopes, FX, and output route are pad-wide.
For Sample, Motion, Grains, Stretch and scanning Wavesets, **AUDITION EDIT LAYER**
(or LOOP push with EDIT encoders on Stack Path) isolates the displayed layer;
performance pads use the saved source mode and play the complete stack.
Stretch keeps its within-source duration separate from the stack cycle; both
neighboring grains receive the same pitch-compensated window duration.
Wavesets offers PRIMARY LAYER or STACK PATH. With Stack Path, two cycle processors blend
adjacent layers; each incoming layer begins its own cycles at its trimmed start.
Analysis runs on the file worker, never in the audio callback. Wavesets remains
disabled for ACN/SN3D. Its scan supports FREE/HOST, HOLD/TOGGLE/Shot Length and
gesture attack/release. Layers without usable cycle analysis remain silent.
Slice Sequence always uses layer 1 and its slice markers, independent of the
edit layer; it does not scan the stack. Choosing Sample disables Stack Path.

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
use **EDIT VIEW → ROUTING → VELOCITY → PAD VELOCITY** for dynamic cell loudness.
No state/parameter changes; Tracker's normalization is unchanged. See the
[Utility velocity notes](../clap_utility_neon_midi/README.md#hardware-velocity).

Version 0.22.1 adds the **WAVE** menu in the waveform panel header, on every page:

- **COMBINED PEAKS**: one min/max envelope spanning all channels.
  This is the previous mono/stereo view, not an audio sum, average or dynamically
  chosen loudest channel. Opposite-polarity channels do not cancel visually.
- **CHANNEL 1** / **ACN 0**: only the first file channel, with no automatic switching.
- **ALL CHANNELS** (default since 0.29.1): one row per source channel inside the same waveform window:
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

## Three workspaces, one set of cells

PLAY is the combined performance/editing workspace. CHOP and RESAMPLE operate
on those same cells, not additional instruments or competing slice engines. Selecting an
editing mode preserves the selected cell. Cell and CHOP bank positions are
remembered separately. Ordinary MIDI notes always play the cells, regardless
of the displayed editing page.

| Hardware button | Primary layer | Secondary layer |
| --- | --- | --- |
| Sampler / **PLAY** | Perform 32 cells across banks A–D | Momentary velocity/pressure Pad FX on the selected cell |
| Slicer / **CHOP** | Audition up to 32 slices of the selected source | Marker tools and assignment to empty cells |
| Hot Cue / **STACK within PLAY** | Audition/hold layers of the selected cell; banks address layers | Source editing, Sample/Motion/Grains selection, One Shot/Hold/Toggle and momentary Stop |
| Hot Loop / **RESAMPLE** | Record internal output or track input | Play loaded cells; empty pads choose a capture destination |

Shift is fine/alternate editing, not a secondary-layer switch. Use the Neon's
secondary mode messages, or the miniature GUI's SECOND/PRIMARY button. Clicking
a GUI workspace selects its primary layer, even if already selected.

### Copying cells

Click a cell pad to focus the editor, then **Ctrl-C / Ctrl-V** to copy/paste
(**Cmd-C / Cmd-V** also work on macOS). Right-click a cell for **Copy Cell** /
**Paste Cell** / **Clear Cell**; Control-click works on macOS too. This is available on the
PLAY primary cell pads and the RESAMPLE secondary pads, not CHOP slice/tool pads.
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
remain the main visual workspace. PLAY includes direct waveform editing;
PERFORM encoders never change trim.
Controls occupy bars outside the waveform. CHOP offers **Live Markers**,
**Transients**, **Equal** and **Beat Grid**. Its banks address slices 1–8,
9–16, 17–24 and 25–32. Assign One/All commits each slice as a **new cropped
sample**, with Start/End reset to 0–100%, a one-slice map and fitted waveform.
Select its pad in PLAY, then open CHOP to edit or re-slice only that
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
This works in PLAY, CHOP and capture review and does not move the edit
cursor, trim or slices. Hardware TRAX zoom still focuses the edit cursor.
Zoom and pan are view-only, not saved sound settings.

Physical Hot Cue primary pads perform stack layers; secondary pads apply their change/page selection and retrigger the
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

- **PLAY:** Filter, Echo, Space, Shift, Vowel, Punch, Drive, Crush.
- **CHOP:** Add, Delete, Audition, Zero Cross, Equal, Transients, Assign One, Assign All.
- **Physical Hot Cue / edit tools:** Source, Sample, Motion, Grains, One Shot, Hold, Toggle, Stop.
  In **EDIT VIEW → Character FX**, secondary pads instead select the eight FX.
- **RESAMPLE primary:** Record, Stop, Audition, Discard, Bus −, Bus +, Review, Assign.
  Secondary pads play loaded cells or select empty targets in the current cell bank.

In PLAY, Mode+pad selects One Shot/Toggle/Hold; Slip or Repeat+pad toggles
repeat, and Sync+pad toggles tempo sync. The five small LEDs keep their physical
**One Shot / Toggle / Hold / Loop / Sync** order: loop is the fourth lamp.
Censor reverses new direct-sample gestures while held. Generated techniques
use their own path/order/shape controls. Releases remain attached to the
original voice even if the page or bank changes during a hold.

| Workspace / encoder assignment | LOOP turn / push | TRAX turn / push |
| --- | --- | --- |
| PLAY / PERFORM | Selected-cell gain / no trim edit | Global Mangle / no trim edit |
| STACK | Layer position / resume saved navigation | Layer transition slew |
| CHOP | Cursor / add marker; Shift+push deletes | Zoom / reset; Shift+push focuses slice |
| EDIT Source/Trim | Cursor / set Start; Shift+push sets End | Zoom / reset; Shift+push focuses trim |
| EDIT Motion settings | Scan offset / audition | Cycle (seconds or beats) / no trim edit |
| EDIT Grains settings | Grain size / audition | Density (Hz) or host interval / no trim edit |
| EDIT Slice Sequence | Step rate / audition | Repeats / no trim edit |
| EDIT Stretch | Duration / audition | Tune / no trim edit |
| EDIT Wavesets | Group cycles / audition | Repeats / no trim edit |
| EDIT Character FX | Named pair's first control / audition | Named pair's second control / next pair |
| RESAMPLE | Review cursor / Start; Shift+push End | Zoom / reset |

All EDIT rows above are encoder assignments inside PLAY, not separate workspaces.
EDIT encoders follow the right toolbox's Source / Stack or playback view, on either pad layer.
Shift gives fine cursor/continuous-parameter steps. Both 1/127 and 65/63
relative encoder streams are accepted; waveform zoom spans 1x–32x.

## Source normalization

**EDIT VIEW → SOURCE / STACK → NORMALIZE** offers three scopes:

- **EDIT LAYER:** normalize the layer selected when Apply is pressed.
- **STACK / EACH LAYER:** independently normalize every loaded layer to −1 dBFS.
- **STACK / KEEP BALANCE:** apply one gain to the stack, placing its highest
  peak at −1 dBFS while retaining relative levels (useful for velocity layers).

**APPLY NORMALIZE / -1 dBFS** processes entire source samples, before pad gain,
envelopes and effects, not just their trim windows. Channels within each sample
share one positive gain, preserving stereo/quad/octo and ACN/SN3D relationships;
this does not convert ambisonic normalization or match perceived loudness.
Trim, slices, playback, routing and edit selection are retained. Original disk
files and shared copies in other pads are untouched; changed samples become
embedded generated audio (PROJECT storage can collect them). Silent/missing
layers are skipped. All replacements and analyses are prepared before a single
stack snapshot is published; allocation or budget failure leaves the stack
unchanged. Playback of the pad stops on commit.

## Resampling

Record the selected internal bus **after** cell processing and master gain.
Use **PLAY PADS** on the miniature NEON (the RESAMPLE secondary layer) for banks
A–D while recording; the duplicate toolbox grid has been removed. The bank
buttons show the number of loaded cells. A warning identifies a selected
cell routed to a different bus from the recorder; routing is never changed
automatically. All playback techniques, One Shot/Hold/Toggle and
multichannel behavior applies. Playing a source never changes the capture target.

On hardware, RESAMPLE primary pads keep their recording tools. Its secondary
layer plays loaded cells and uses empty pads to choose a destination (and audition
a completed take). Releases stay attached to the original cell across bank/page
changes, so Hold does not latch accidentally. The miniature NEON follows this
same mapping. The mini layer button is labeled **PLAY PADS / REC TOOLS** here.
Record/Stop remain available in the toolbox on either layer. Hardware
**SHIFT + pad 1 (REC)** toggles recording on both RESAMPLE layers without playing
pad 1 or changing the capture destination. Pressure/release do not toggle it.
In Review Take mode, a completed take is protected; explicitly Discard before
recording another review. Pad Stack records into layers and shows the latest
completed take in review; Undo retains the previous one. Primary Record also
toggles; Shift+pad 2 takes the next layer.

Stop is also available on the miniature controller while recording, regardless
of the page. Both waveform views draw **during recording**, with a moving write
cursor and stacked CH/ACN rows for multichannel captures. The first second fills
left-to-right; longer takes fit the growing recording. Display peaks are published
through a bounded atomic buffer, not read from unfinished audio storage.
In Review Take mode, stopping hands
off a linked-channel asset for audition, trim and explicit assignment to an
empty cell. Review playback goes to bus 1 of a matching layout and is excluded
from recording. Changing the global layout stops an internal-output take;
Track Input uses its own format declaration. Discard the review before starting
another Review Take; already assigned cells are kept. Pad Stack mode instead
commits each completed take directly to its chosen empty layer.

Set Start/End, then use **CROP TO SELECTION / DISCARD OUTSIDE** to remove unused
audio from the review take. Hardware **Shift + RESAMPLE primary pad 7** performs
the same crop. It uses identical integer-frame bounds across all channels,
resets the retained take to 0–100%, resets zoom, and auditions the result.
Already assigned cells retain their original immutable audio. Secondary target
pads and the Review action also audition the capture. Cropping is destructive
for the current review; Undo restores its original audio within this session's
history limits. Save a set for durable recovery of the untrimmed take.

The recorder preserves channel width and order, including all 16 ACN/SN3D
channels. Neon Output records internally; Track Input records upstream REAPER
audio/effects, never effects placed after Neon.
Capture is block-accurate, preallocated and limited to 30 seconds or 1,440,000
frames, whichever is smaller (15 seconds at 96 kHz). Finalization and assignment
run on the main thread, not the audio callback.

## Starting over

**RESET ALL** in the header opens a confirmation with **CANCEL / CLEAR ALL**.
Stop recording first. Confirming stops playback, clears all 32 cells and the review take,
and resets sound parameters, slice maps, stages and editing positions. The
output layout, base MIDI note, receive channel and Neon ownership are retained.
Imported files are never deleted. Undo restores the work within this session's
history limits; save a set for durable recovery. Reset waits for the audio callback to stop before clearing
assets, and invalidates pending file loads so cleared cells cannot refill later.

## One playback technique per cell

The **PLAYBACK** menu in the right toolbox is the authority. Its choices are
**Sample**, **Lanes**, **Motion**, **Grains**, **Stretch**, **Wavesets**, **Slice Sequence**, **Spectral**, and **Cutups**;
choosing one commits to it and replaces the previous technique. EDIT pads 2–4
remain fast Sample/Motion/Grains shortcuts; the menu exposes all nine.
**EDIT VIEW → Source / Stack** (or secondary pad 1) changes only what is being
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
  with the sample; Hold/Toggle loop. PRIMARY LAYER is free-running; STACK PATH adds
  Free/Host, Shot Length and gesture envelopes. Both are disabled for
  Ambisonics. Analysis runs on the file worker; silent/DC sources may lack
  usable cycles. It supports discrete mono/stereo/quad/octo channel order.

## Spectral, Mosaic and cycle oscillator (0.35)

- **Spectral** is phase-continuous, 1024-point FFT resynthesis, not a grain
  repeat. POSITION selects the analysed moment within the trimmed source.
  ADVANCE at zero freezes it; raising Advance traverses source frames, with
  SOURCE CYCLE setting the full-speed duration. PRESS ADV adds pad pressure to
  Advance, capped at full speed. BLUR smooths the joint spectral energy
  envelope and slows spectral changes; high Blur softens detail and level.
  The FFT window introduces a short attack build-up (about 21 ms at 48 kHz).
  LOOP edits Position; TRAX edits Blur in EDIT encoder mode. Shift gives fine
  adjustments. Primary Layer, Edit Layer, Velocity, Random / Trigger and Stack Path
  remain available. Stack Path blends neighbouring layers at the same source
  position. All source channels share frame positions, bin gains and phase
  rotations; ACN/SN3D remains channel-linked. No independent-channel phase
  randomisation or decoding is performed.
- **Slice Sequence → NAVIGATION** now includes **Similar**, **Contrast**,
  **Energy** and **Brightness**. These are Mosaic navigation choices inside
  the existing slice sequencer, not a second sequencer. CANDIDATES selects the
  primary layer's authored slices or authored slices throughout this pad's
  stack. An unsliced layer contributes one whole trimmed region. Energy and
  Brightness use TARGET; Similar/Contrast compare against the preceding
  fragment. Ties are deterministic; immediate repeats are avoided when another
  candidate exists, while REPEATS explicitly holds a selection. STEP, CHANCE,
  the per-slice proportional ADSR and protected handoff fades still apply.
  The waveform follows the chosen layer; the selected edit layer is unchanged.
  Descriptors are bounded channel-energy/adjacent-difference estimates prepared
  with the source snapshot, not full-file perceptual analysis on the audio thread.
- **Wavesets → ENGINE → Oscillator** turns the selected cycle GROUP into a
  sustained, band-limited wavetable. CYCLE POS selects its source group; SCAN
  moves through groups over SOURCE CYCLE. FREQUENCY tunes the whole group and
  the common pad Tune transposes it. A multi-cycle group can therefore have
  prominent harmonics above that fundamental. LOOP edits Cycle Position;
  TRAX edits Frequency logarithmically. It retains Primary Layer/Stack Path and
  the stack breakpoint path, with shared cycle boundaries across channels.
  Like Rearrange, it is discrete-only; silence/DC may have no usable cycles.
  Cycle changes crossfade tables. Rearrange retains its original controls.

Spectral and Oscillator share Free/Host clock, Attack/Release, Hold/Toggle and
explicit Shot Length. In Host mode, transport pause freezes advancement and
silences output; Free mode runs without transport. Ordinary Tracker notes address
pad cells at their configured oscillator pitch; 0.41's chromatic channel routes
add note-relative transposition.
Normal routing, Character FX, resampling and Fill Hold remain downstream.
New controls use append-only state version 22 when needed; older states keep
their existing field widths, sound controls and CLAP/parameter identities.
There is no Resonator engine in this version.

### Spectral colour (0.36)

In **Edit View → Playback**, Spectral now groups source navigation first,
spectral shaping next, and pad duration/envelopes last. The waveform stays its
existing size; row spacing and double-click default resets are unchanged.

- **SMEAR** (0–4 seconds) retains the previous spectral frame as the source or
  stack changes. It is a spectral response time, not pad Release or an echo.
  Zero retains the original Blur response. A stationary frozen frame will not
  demonstrate Smear: use Advance, pressure, Position or Stack Path to hear it.
- **FOCUS** (−1 to +1) flattens quieter spectral detail towards the peaks at
  negative values, or emphasizes dominant peaks at positive values. Joint
  energy compensation keeps it from being merely a volume control; boosts
  are bounded and silence stays silent.
- **TILT/OCT** (−6 to +6 dB/octave) darkens or brightens around 1 kHz. The spectral
  gain is bounded to −24/+12 dB. It does not move partial frequencies or change
  pad tuning; allow output headroom for positive gain.
- **THIN** (0–1) progressively removes deterministic three-bin bands for
  hollow, sparse textures. It does not randomize each trigger or channel.
  High settings may remove most or all of a narrow-band source.

Colour changes are hop-smoothed independently of Smear, so held-pad edits
remain responsive. All four controls are per-pad and share gains/phase handling
across source channels, including ACN/SN3D; they never mix spatial components.
The EDIT encoders remain LOOP = Position and TRAX = Blur (Shift = fine).
New controls default to zero and only require state version 23 when changed;
version 22 retains its exact stored width and default sound. Copy/paste, reset,
project recall and stored pad stacks include the new controls.

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
windows. Find them in the Playback view. Releasing during
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
Wavesets PRIMARY LAYER is always Free. Wavesets STACK PATH also supports Sync+pad
and the Host-clock lamp.

## Character FX: playback → effect → output

Character is now a real post-playback processor, shared by every playback
technique, rather than an alternative set of trigger parameters. Reverse is
kept in playback and Hold/Toggle in Trigger; neither is duplicated as a Character.

Open **EDIT VIEW → Character FX** inside PLAY. Each cell retains a complete
settings bank for each of the eight effects; switching effects recalls those
settings. MIX, OUT and PRESSURE stay in the same positions above the effect's
own controls. MIX blends dry/wet; pressure and global Mangle add to it. OUT trims
the resulting wet/dry signal (−36 to +12 dB). Continuous controls and Mix are
smoothed. One Character runs per cell, not an eight-effect chain.

| Effect / pad | Parameters | ACN/SN3D |
| --- | --- | --- |
| Filter / 1 | Low/High/Band Pass or centre-neutral DJ; Sweep, Resonance, envelope Depth/Release | Enabled |
| Echo / 2 | Digital/Tape/Multi Tap/Reverse; Time, Feedback, Damping, Diffusion, Duck, Glide, Free/Beats clock, Division | Tape disabled |
| Space / 3 | Room/Plate/Diffuse; Size, Decay, Pre Delay, Damping | Enabled |
| Shift / 4 | Detune/Octave/Dual; Pitch, Detune, Window, Balance | Enabled |
| Vowel / 5 | Continuous AH/EH/EE/OH/OO morph; Throat, Resonance, envelope Depth | Enabled |
| Punch / 6 | Linked Break Bus parallel compression and transient shaping: Press, Snap, Recovery, Body | Enabled |
| Drive / 7 | Eight Macro Shred circuits; Drive, Shred, Feedback, Color, React, Tune, Body | Disabled |
| Crush / 8 | Kit-style quantize → rate hold → drive; Bits, Rate, Jitter, Drive, optional Tone low-pass | Disabled |

The first six use shared coefficients, timing, modulation and linked envelope
detection across channels. Punch uses a linked gain envelope with its saturation
and clipping disabled; it does not compress components independently. Drive,
Crush and Echo's Tape feedback are nonlinear per component and can alter
ambisonic spatial relationships, so those choices are disabled for ACN/SN3D.
Selecting that format clears an unsafe Character or Wavesets choice. State recall,
parameter input and DSP also enforce the restriction. There is no implicit decode,
encode or normalization conversion. Discrete quad/octo permit all eight effects.

Echo offers free times from 5 ms to 2 seconds, or host-tempo divisions from 1/32
to one bar. Beat times are capped at the same 2-second buffer limit; the readout
shows MAX when capped. Glide smooths time changes. Shift is a post-playback
dual-head delay pitch effect, not source tuning or a time-stretch engine.
Space uses linked reverberation tanks without implicit stereo widening.

On the FX page, LOOP/TRAX edit the named controls in Encoder Pair. Push TRAX
to advance through pairs, including OUT/MIX, or choose the pair from its menu.
In Echo's Beats clock, the time encoder edits Division instead of inactive free time.
Push LOOP to audition. Secondary pads
choose effects and apply momentary pressure amount; primary pads still play cells.
Click numeric values to enter real units; double-click sliders to restore defaults.
The DJ sweep resets to its neutral centre. RESET THIS EFFECT resets that effect's
settings and OUT without changing Mix, Pressure, playback or other effect banks.
The page fits the existing 16:9 canvas without resizing the waveform.

Version 0.34 intentionally replaces the old Character processors. Loading older
sets/projects preserves sources, stacks, playback and routing, but resets all
Character banks to new defaults and cell/global Mangle to zero. Old Character
sounds are not preserved. State with new effect settings uses schema 21 and
requires 0.34 or later; save a separate project copy before migrating.

## Loading and saving

Drop one or more supported audio files onto the active bank's pads. A multi-file
drop fills consecutive slots from the target pad, including following banks.
Loading and decoding happen on a worker thread, so the audio process is not
blocked. Each slot stores gain, pan, tune, Start/End, envelope, filter,
Mangle, pressure depth, character, trigger mode, repeat/sync/velocity, choke
group, shared chop layout, source format and output-bus settings. Playback
technique, clock, Shot Length and all technique settings are saved as well.
The plug-in ID and existing host parameter IDs are retained. Per-effect and
playback settings are saved in sets/project state. Current extended Character
settings use schema 21 (0.34); other extensions select their applicable schema.
Versions 0.13–0.33 still load their sample/playback data, but their old Character
settings are **not sound-compatible**: cell/global Mangle reset to zero and the
new effect banks use defaults.
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
