# GUI design contract

The tracker uses the `s3g-dsp` Cocoa visual language while keeping native
macOS window, input, menu, focus, and accessibility behavior. The goal is a
dense instrument surface, not a collection of stock AppKit controls.

## Visual language

- Bundled IBM Plex Mono Regular/Medium/SemiBold, with the monospaced system
  font as fallback. The shared font boundary raises legacy compact sizes by
  16 percent with an 8-point floor. Interface labels are uppercase; hierarchy
  comes primarily from size and tone.
- The `Night Tracker` palette uses a neutral near-black hierarchy: canvas
  `#060606`, workspace `#0a0a0a`, panel `#101010`, raised surface `#181818`,
  control `#262626`, grid `#303030`, and border `#4c4c4c`. Primary text is a
  soft `#dededa`; secondary, muted, and faint text step through `#bababa`,
  `#878787`, and `#656565`. Structural colors do not carry a purple or blue
  cast.
- Semantic accents are sparse and stable: neutral light gray `#c0c0bc` is
  keyboard focus, cyan `#7fd7e8` is general live state or signal flow, cyan
  `#85cbd3` labels NOTE structure, neutral `#b5b5b1` labels
  instruments, yellow `#e8d47d` labels values, and green/orange/red indicate
  success/warning/danger. Tracker cells deliberately restore the v8 semantic
  palette: muted green `#2e412e` for playback, blue-purple `#303854` for a
  dragged selection, and muted purple `#4d4d6b` for the editing cursor. These
  colors do not replace the grayscale structural palette. Lane identity
  colors remain the only repeating per-track color family.
- The direction is informed by
  [Dracula](https://github.com/dracula/dracula-theme)'s readable foreground and
  semantic accent family and
  [Monokai Night](https://github.com/fabiospampinato/vscode-monokai-night)'s
  deep structural blacks and reduced borders. No theme source code or assets
  are embedded; tracker tokens are defined centrally in
  `s3g_tracker_controls.h/.mm`.
- Panels use a one-pixel frame and a restrained two-pixel top accent.
- Buttons and popups use flat square custom faces with native target/action,
  menus, keyboard focus, and accessibility semantics intact.
- Ordinary controls and rack identity remain neutral; color is reserved for
  edit focus, live state, data type, or status. A restrained lane palette
  identifies tracker and mixer lanes. Rhythm Geometry uses the corresponding
  vibrant eight-color v8 ring palette so overlapping pulse polygons remain
  distinguishable; warnings/errors retain their own semantic treatment.
- Twelve-point module gaps and compact label/value rows are the default.
  Avoid nested cards and decorative borders that reduce tracker density.

The palette and typography follow `s3g-dsp/docs/gui-style-guide.md` and the
shared constants in `s3g-dsp/plugins/common/s3g_cocoa_gui.h`. The tracker-local
controls are an initial standalone implementation; they can move into a shared
`s3g-dsp` Cocoa controls target once another app needs the same classes.

## Workspace hierarchy

The main window has five horizontal bands and one persistent right toolbox:

1. Transport, tempo, swing, gate, aggregate device status, and module actions.
2. A single-line live-code command entry directly beneath transport.
3. The editable tracker grid as the largest uninterrupted surface, with
   a readable `NOTE/INS/VOL` performance page plus `FX1/V1` and `FX2/V2`
   pages rather than seven permanently squeezed columns per lane.
4. A selected-lane contextual envelope. It edits
   normalized volume on the first page and the corresponding FX value on FX pages.
5. A full-width bottom Device View following the selected song instrument.
   It shows the device-to-master chain, preset menus where available, and the
   endpoint/channel owned by a MIDI OUT instance. FX A/B are visible reserved
   slots, not yet active insert processors.

The right toolbox contains `SONG INDEX`, `AVAILABLE`, `TRACKER ZOOM`, and a
separate scrollable `CONSOLE OUTPUT` readout below the instrument toolbox.
Command typing and history remain in the live-code strip beside transport;
commands, results, completion matches, status messages, and errors print only
in the right readout. The live-code entry uses the same mono face at two points
larger than standard text fields for rapid performance typing. The song index
starts with one Membrane Kick, Stereo
Slice Sampler, and MIDI OUT;
clicking an indexed row assigns the selected track's default. `+ ADD` creates
another instance at the next decimal index when the engine has capacity.
Double-clicking an internal instrument row or clicking `↗` opens its detailed
editor; MIDI selection exposes its route controls in Device View. The toolbox
does not magnify with the tracker and scrolls its instrument/library region
when the active index outgrows the available height.

The `MIXER` action replaces bands 3 and 4 with a horizontally scrollable
performance mixer while transport, live-code entry, console output, and the
instrument toolbox remain available.
Up to 32 grayscale track strips have consistent gutters, a muted lane-color
top cap, and a light selected outline. A wider `MAIN OUT` strip is visually
and semantically separate.

Rhythm Geometry is a retained pop-out window, available from the toolbar or
Window menu. It shares the exact live `TrackerViewState` with the workspace;
it is neither a copied model nor a second scheduler. Closing Geometry hides
that view without stopping playback, and reopening it preserves its window
position and state. Each lane is drawn as the polygon joining its pulse
vertices, without static vertex dots. During playback, one small filled
muted-yellow point appears at the current vertex only when the sequencer
actually emits a NOTE onset. It disappears on rests, muted NOTE columns, and
unresolved retriggers. Geometry has no continuously moving per-step cursor, so
the yellow point has exactly one meaning: a hit just occurred on that lane.
Each polygon uses its lane index in the vibrant v8 ring palette; the window
does not fill the panels with track color. Muting a lane's NOTE column removes
that lane's polygon and legend row completely, and the remaining visible
polygons expand to use the vacated geometry.

Functional Timing Warps is a retained pop-out, available from `WARPS`, the
Window menu, or Command-8. A large graph shows the composite phase curve over
a dashed identity line with tick guides. The adjacent stack selects, adds,
removes, and clears exponential, stepped, and Euclidean transforms; the lower
editor exposes type-specific primary values plus mix, segment, and repeat.
Every edit recompiles and publishes the same bounded timing stack used by the
console and live scheduler.

Membrane Kick is a second retained pop-out, available from the toolbar
or with Command-4. It presents only Membrane Kick instances that were added to
the song index; a new song therefore has one `00 MEMBRANE KICK` tab. The custom
editor groups normalized base-patch
controls into Body, Impact, Strike, Space, and Response. Its membrane outline
and strike point are editable geometry, not decoration. Parameter bars use the
same flat custom drawing as the workspace rather than stock Cocoa sliders.
The editor publishes bounded control changes to the live audio engine and
never owns scheduling or CLAP processing.
Its custom-drawn tabs, parameter bars, and action buttons expose native
accessibility roles, labels, values, and actions rather than presenting one
opaque canvas to assistive technology.

Stereo Slice Sampler is a retained native editor, available by expanding an
indexed sampler, the Window menu, or Command-7. Indexed tabs expose only
samplers added to the song. The editor loads mono/stereo audio, draws its
waveform and slice markers, creates common equal-slice layouts, edits base-note
mapping and per-slice reverse, and auditions the selected slice.

MIDI Instrument is a retained route editor, available by expanding an indexed
MIDI OUT instrument, the Window menu, or Command-9. Its flat popups select the
song index, any of eight owned `s3g Tracker N` CoreMIDI sources or a connected
physical destination, and channel 1–16. The route diagram is informational;
all edits still use the rack model and cleanup-aware playback callback.
`AUDITION C4` sends a 150 ms note on the selected route while transport is
stopped.

Song Mode is a retained, nonmodal alternate window. `SONG TRANSPORT` explicitly
chooses whether the main Play button runs the draft; ordinary Pattern playback
remains the default. Rows set duration/repeats, optional BPM and swing, and a
32-lane mute mask. The live row has a green stripe and a queued quantized row a
yellow stripe. Structural editing locks during an active/paused Song so row
indices cannot change underneath the scheduler. Song rows select stable IDs
from the schema-version-2 pattern bank through a constrained popup. Missing
references fail with the exact row instead of substituting material. The bank
selector beside transport exposes New, Duplicate, Rename, and guarded Delete;
deletion cannot leave a dangling Song row. Before Play, the transport resolves
each row to a frozen prepared slot.
Cross-pattern rows then launch at their logical boundary; authoring edits made
during an active Song are deferred until that frozen performance is released.

Console Help is a retained reference window, available from the toolbar
`HELP` button, the Window menu, or Command-5. It uses the tracker palette and
monospaced hierarchy rather than a stock table, while retaining native text
selection, accessibility, keyboard scrolling, and Escape-to-close behavior.
Its categorized contents and the console's `help` response share the same
top-level command registry.

## Interaction rules

- The grid retains complete keyboard editing and owns focus after Escape from
  the console. Typing `0`–`9` or `A`–`G` on NOTE opens a validated inline
  editor immediately; it accepts MIDI `0..127`, tracker names such as `C-4`,
  compact names such as `C4`, and sharps/flats. Return or double-click opens
  exact entry on any field; Escape cancels and Return commits/advances.
- Dragging across tracker cells creates a rectangular selection.
  `Command-C`, `Command-X`, and `Command-V` copy, cut, and paste tab/newline
  separated typed cells; `Command-A` selects the visible page. A single copied
  cell fills a selected block. Home/End, Page Up/Down, and F9–F12 provide pattern navigation.
  `Command-+`, `Command--`, and `Command-0` zoom the tracker to 55–180% or
  reset it; the right toolbox also exposes `− / percent / +` controls.
- Tracker lanes reserve a noninteractive gutter and inner padding between
  their panels; no extra divider line is drawn in that gap. Drawing, header
  controls, cell hit-testing, playheads,
  and accessibility geometry all derive from the same lane bounds; the
  selected lane receives a full-height light outline and header underline.
- Every fourth row receives the stronger beat tone and horizontal guide. The
  selected row keeps one quiet band across the complete tracker. Within it,
  the editing cursor uses muted purple, a dragged block uses the darker
  blue-purple selection fill, and each advancing column playhead uses muted
  green. Cursor and selection take visual precedence when states coincide.
  This separates row navigation from playback and cell editing and
  makes long polymetric rows easier to scan laterally.
- The page control cycles `NOTE/INS/VOL -> FX1/V1 -> FX2/V2`.
  Tab/Shift-Tab traverses each field and wraps across those pages. Selection,
  field-specific mute state, and independent playheads remain visible on every
  page. The first page gives the narrow middle `INS` field a decimal `I00`
  style index rather than hexadecimal instrument numbers.
- Double-clicking a NOTE, INS, VOL, FX1, V1, FX2, or V2 header opens exact
  decimal length entry for that field. Values `1..256` commit on Return and
  publish without changing the other six polymetric lengths.
- `INS` is row state, not a lane label. Its cells, independent playhead, and
  mute/length/direction status remain visible beside NOTE and VOL. Typing any
  digit starts buffered zero-based decimal index entry (`00`, `11`, etc.);
  Delete clears, `R` writes Previous, and brackets cycle populated indices.
- `VOL` cells display and accept normalized decimal values `0.000..1.000`.
  Brackets adjust by `0.05`; conversion to MIDI velocity `0..127` occurs only
  at the MIDI output edge.
- FX action cells show catalog mnemonics, FX values use normalized decimals,
  and stable action keys plus supported scopes remain available through the
  live console. The current membrane catalog supports Global scope only.
- FX playheads and memory still advance on MIDI OUT lanes, but internal
  parameter actions do not become MIDI CC. Instrument kind determines output;
  there is no user-facing per-track `Both` switch.
- `:` or backtick focuses the console without requiring a mouse trip.
- Membrane editor number keys select active instances, Space auditions one,
  `R` restores that instance's kick patch, and five preset buttons replace its
  complete base patch. Double-clicking a parameter restores
  that parameter's default. Tab and Shift-Tab traverse every
  control; arrows adjust a focused parameter, Shift-arrows make fine changes,
  and Return activates a focused instance or action button. Keyboard, mouse, and
  accessibility focus share the same visible outline.
- Auxiliary windows never own musical time or duplicate scheduler state.
- Mixer track faders are labelled `VEL / INPUT`: they scale future canonical
  Note On velocity for both MIDI and internal destinations, not rendered
  audio or existing tails. Their UI floor is MIDI velocity 1/127; NOTE mute
  provides silence. Track M/S controls gate NOTE onsets while the other
  polymetric fields continue, and
  `STEP ACT` is not a dB meter. Only `MAIN OUT` is post-decode audio gain;
  its mute and aggregate peak concern internal audio, and MIDI bypass is
  stated in the strip.
- Main-window closure stops playback; closing Geometry, Song, Membrane Kick,
  or Stereo Slice Sampler does not.
- Instrument-owned endpoint/channel changes, Stop, Panic, and termination keep
  their MIDI cleanup behavior regardless of which auxiliary windows are visible.
- Every custom control must keep a visible focus state, a practical hit target,
  and an accessibility role, value, and action. Controls implemented as native
  subviews retain target/action or menu behavior beneath their flat faces.
