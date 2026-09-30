# s3g Utility Neon MIDI

Version 0.5.1. MIDI-only CLAP companion for one or two Reloop NEONs. No firmware change,
audio processing, hardware MIDI output or second LED owner. Uses the same
factory-message decoder as Sample Neon 32. Utility-family Fira Code/grayscale
editor: 10 pt body, 15 px menu fields, 18 px dropdown rows, 65–200% resizing.

## One-track REAPER setup

Place these **in this order in the normal track FX chain**:

1. **s3g Utility Neon MIDI 0.5.1** — Control Route `TRACKER + SAMPLE NEON`,
   Base Note `36`, Output Channel `1`.
2. **s3g Tracker 0.4.1 or newer** — participating lanes, including the recording
   destination, on `CH01`.
3. **s3g Sample Neon 32 0.37.0 or newer** — Follow Utility map, Standard MIDI Receive
   `1`, and `NEON OWNER: ON` in this one instrument instance.

Choose the physical NEON as the track's MIDI input, on all its native channels;
enable input monitoring (and arm the track if required by the REAPER setup).
Do not remap input channels before the Utility. No parallel raw-NEON send
to the instrument: that would duplicate the controller path. On Mac, **no MIDI
hardware send back to NEON**; Sample Neon owns direct CoreMIDI LED output.
In the default `HOST MIDI` mode, Utility does not connect to device inputs.
The optional USB modes below open NEON inputs directly on macOS.

Use the hardware's **primary SAMPLE layer** to record performances. `REC OFF`
in updated Tracker passes notes without writing a pattern. `REC STEP`,
`REC Q`, and `REC MT` retain their recording behavior and monitor each
input note once, on the armed lane's channel. Live Q/MT require running host
transport. Hold-mode samples follow recorded gates/releases; One Shot and Toggle
retain their own playback policies.

| Bank | Cells | Default MIDI notes |
| --- | --- | --- |
| A | A1–A8 | 36–43 |
| B | B1–B8 | 44–51 |
| C | C1–C8 | 52–59 |
| D | D1–D8 | 60–67 |

The Utility is the **performance-bank authority**: select banks on the hardware
or its A/B/C/D buttons. It sends that selection downstream, including after
project recall. Sample Neon's GUI bank selection is not a return connection to
the Utility; the next controller message restores the Utility bank. CHOP's
separate slice bank and HOT CUE's STACK layer bank never change the performance bank. This is one-way
synchronization, not automatic bidirectional linking.

In 0.1.3, HOT CUE bank buttons remain private STACK controls rather than
changing the musical sample bank. Returning to SAMPLER restores its last bank.
STACK layer gestures, modifiers and encoders continue through the private
Sample Neon route; they do not become Tracker notes or recorded automation.

## Independent Keyboard roles and layouts (0.5.0)

The title-row KEYBOARD button opens the unit-role settings. Each unit can use
PAD CELLS (the existing custom map) or KEYBOARD (32 keys across banks A–D).
Each unit has its own LAYOUT:

- **CHROMATIC:** consecutive semitones from FIRST NOTE (default 48).
- **SCALE:** the shared 101-scale s3g-dsp list in its canonical order. ROOT NOTE
  is the MIDI note on A1 and defines the tonic/octave. Each following pad plays
  the next scale degree, continuing through banks B, C and D. For example,
  Major from 48 starts `48, 50, 52, 53, 55, 57, 59, 60`; B1 is 62.
  Notes above MIDI 127 are OFF, never wrapped or clamped to a repeated pitch.
- **MANUAL:** EDIT MANUAL NOTES opens all 32 banked keys. Enter MIDI 0–127,
  or -1 for OFF. Repeated pitches are allowed; a shared key remains held until
  the last finger releases it. COPY/PASTE LIST uses 32 bank-major integers.
  FROM SCALE fills the draft from the current root/scale; FROM A1 fills a
  chromatic run. APPLY commits and selects Manual; CANCEL leaves it unchanged.
  Manual notes ignore First Note/Scale, and remain stored when using another layout.

The Keyboard page shows all 32 resolved keys; the main monitor shows the active
bank. The existing PAD NOTE MAP remains separate: it maps notes to sample cells,
requires unique notes, and is shared by both units. Manual Keyboard maps instead
choose pitches for a pinned sample, separately for each unit.

With Utility **0.5.1** and Sample Neon **0.41.2**, A–D on a KEYBOARD unit's
primary SAMPLE page select only its eight-key range. Its bank LEDs follow that
range; Sample Neon's selected pad, sample bank and editing page do not change.
The other NEON can remain in PAD CELLS for normal navigation. This also works
with one unit and with Utility's on-screen bank selection. No extra switch is
needed. A deliberate CHOP, STACK, RESAMPLE or secondary-page press still enters
editing; pressing SAMPLE returns to the isolated keyboard. Held notes keep
their original pitch and channel across range changes. In Manual layout, A–D
select the corresponding eight assignments rather than transposing them.

Unit 2 can have its own output channel
or FOLLOW UNIT 1, which preserves existing setups. Velocity and pitched poly
aftertouch are sent as ordinary MIDI; held releases retain the original key and
channel after changing bank/range/role. Editing pages still send controls only.

Example: Unit 1 PAD CELLS on CH1; Unit 2 KEYBOARD on CH2. In Sample Neon 0.41,
ROUTING → NOTES / VOICES: MIDI IN OMNI, CH1 PAD NOTE MAP, CH2 CHROMATIC A1.
This pins the pitch keyboard to A1 regardless of the selected edit cell. In
Tracker, record the keyboard on a CH2 lane (Tracker monitors on its armed lane's
channel). REC OFF retains incoming channels. The same scheme works with one
NEON by changing its role; it does not require two devices.

Keyboard mode deliberately does not send Select Cell for each pitch. Private
Keyboard setup messages allow Sample Neon to light the pinned cell's playable
keys and root. In NOTES ONLY mode, Keyboard notes/velocity/poly aftertouch still
work, but there is no Sample Neon control/LED synchronization. Source pad mode
retains its existing private aftertouch behavior.

Parameter IDs 9–13 / state v4 added role/range/channel settings. IDs 14–17 /
state v5 append layouts, scales and both manual maps. All existing IDs and
v1–v4 projects retain their settings; neutral new settings still save in
the older formats. Mapping edits do not change held releases or aftertouch.
Sample Neon **0.41.1** receives the resolved keyboard map for root/playing/OFF
LEDs and modifier audition pitches. Older Neon versions still receive the
correct musical pitches but assume chromatic hardware feedback. No Tracker
upgrade is needed; its existing private-message thru carries the map.

## Controls and recording are separate

The factory SAMPLE pads send the same raw channel-8 notes 0–7 in every bank.
The adapter remembers bank buttons and emits `base + bank*8 + pad` by default,
or the corresponding cell's custom note when a map is applied.
Velocity and sample offsets are retained. Releases latch the original note
and channel even if bank, page, SHIFT or mapping changes under a held finger.

### Custom pad notes (0.3)

On macOS, 0.3.1 keeps VSTGUI and gives the pad-note numeric fields charcoal
selection highlighting with light text. Styling is scoped to that active field;
the host window's shared text editor is restored when entry ends.
Buttons also flash light gray for 180 ms on a recognized press, then return to
their normal active/inactive appearance. Disabled buttons do not respond.

Open **PAD NOTE MAP** in the title row. Edit all 32 MIDI addresses in the A–D
grid, or Paste List with 32 unique integers (0–127) in A1–A8, B1–B8, C1–C8,
D1–D8 order. Commas, whitespace and line breaks are accepted. Copy List exports
the same format; From A1 fills consecutive notes starting at A1 (0–96).
Default restores Base Note mapping. Apply commits the draft and releases held
notes; Cancel leaves the current map unchanged. Custom maps ignore Base Note.

In `TRACKER + SAMPLE NEON`, Utility sends a validated private v3 map envelope
before performance notes. Tracker forwards it; Neon 0.37's default Follow
Utility mode adopts it. Enter the map only in Utility for this chain. There
is no reverse connection from Neon to Utility. Both USB units share the same
32-cell map, while retaining independent banks. Velocity, held-note releases
and private aftertouch retain their existing behavior.

Notes Only uses the custom musical keys but sends no map or control envelopes.
Copy/Paste List can match a local Neon map manually. The map is saved in Utility
state (v3 with 32 notes appended to v2); default maps still save as v2. Old v1/v2
states retain their original behavior. Remapping never changes already-recorded
Tracker patterns. See the [Neon guide](../../docs/sample-neon.html#pad-note-map).

### Hardware velocity

**SHIFT + SAMPLER** on the physical NEON toggles velocity sensitivity. This
hardware setting is separate from Sample Neon's per-cell velocity response.
When it is off, the hardware sends fixed-127 note-ons. When enabled, the
measured strike arrives as a CC (`B7 pad value`) immediately before a note-on
that still contains 127 (`97 pad 7F`). Aftertouch uses `A7`, separately.

Version 0.1.1 pairs the CC with that pad's following note-on and uses the
measured velocity. Version 0.1.0 incorrectly classified the CC as pressure;
its ordinary note-velocity tests missed the actual hardware protocol.
The correction uses no velocity curve, retrigger, extra note or added delay.
Each value is consumed once; pad/channel/page/SHIFT addresses must match.
Pairing crosses audio-block boundaries, but unmatched values expire after
20 ms of audio time. Releases, reset, context/mapping changes and a hardware
velocity toggle clear pending values. A measured zero becomes MIDI velocity 1
so a real strike cannot accidentally become a note-off. Without a matching
CC, the incoming note-on velocity is preserved. Note-on zero still releases.
Editing/audition CCs travel through the private control route to Sample Neon's
matching decoder; musical CCs are consumed in the Utility, not recorded as controls.

The footer displays the last successfully emitted **NOTE**, **VEL** (1–127),
live **AT** aftertouch (0–127), and **VOL** (`VEL / 127`, 0–1): for example,
`NOTE 46 / VEL 003  AT 090  VOL 0.024`. Note, velocity and volume retain the
original strike through pressure changes and release. AT follows incoming
pressure for that same held pad, including across bank/base/channel changes;
other held pads do not overwrite its readout. It starts at zero for a new hit
and clears on release, RELEASE HELD, reset, transport stop or deactivation.
Before any hit, the values show dashes. AT is an **input monitor**, including
in NOTES ONLY; it is not confirmation that the host accepted a control event.
The SHIFT + SAMPLER reminder is not a claim to know the hardware's current mode.

Tracker's live monitor and recorder preserve that velocity. In Sample Neon,
**EDIT → SOURCE/TRIM → VELOCITY → PAD VELOCITY** enables the cell's dynamic
response (the default); **CONSTANT LEVEL** ignores velocity for loudness.
Tracker's existing normalization remains unchanged.

Unmodified primary SAMPLE pads produce musical notes. Encoders, mode/bank
buttons, modifiers, SHIFT actions, secondary tools, CHOP/EDIT audition and
RESAMPLE actions travel as **private host-local SysEx control envelopes**.
Tracker forwards these without recording them. Sample Neon decodes them for
its existing editing/audition actions. Musical presses send a selection update,
not a second raw pad trigger. Held performance pressure reaches Sample Neon
through this control path; Tracker does not write pressure into the pattern.
Live edits/auditions are not a recording of automation or parameter locks.

Choose `NOTES ONLY` for a different instrument or a separate recording branch.
Then control envelopes, including Pad Cells aftertouch, are suppressed.
Keyboard mode emits ordinary pitched MIDI poly aftertouch, which is retained
in Notes Only. Pad Cells aftertouch uses the Sample Neon bridge only.
There is no raw MIDI-thru in the
Utility, and it deliberately does not accept a keyboard or already-translated
notes as an additional input. Connect those separately after the adapter.

`RELEASE HELD` sends note-offs for active musical input. Host reset/transport
stop also releases held notes; output-rejected releases are retried. This is
not an instrument-wide kill for One Shot/Toggle or editor audition; Sample
Neon's KILL remains available. Change base/channel with no keys held and keep
the destination's map matched. Default CH01 avoids Neon's native command
channels. `HOST MIDI` handles one controller on the original input port. Never
merge two raw NEONs into that port and expect independent banks.

## Optional two-USB setup (0.2.0 / Sample Neon 0.30.0)

Keep the same **Utility → Tracker → Sample Neon** chain. Connect each NEON by
USB and remove the Smart Link cable. In Utility's CONTROLLERS toolbox, choose
`USB TWO NEONS`; keep Control Route at `TRACKER + SAMPLE NEON`. Enable
`NEON OWNER` only in the destination Sample Neon 32 instance. No REAPER MIDI
hardware send is needed. Existing host raw MIDI input is ignored in USB modes,
so it cannot double-trigger the USB input. The track must still be processing
and monitored for the plugin chain to play/record.

- Unit 1 defaults to A and unit 2 to B, offering 16 performance pads. Both can
  independently choose any bank; disconnecting one does not reassign the other.
- `VIEW: UNIT 1/2` selects which bank, held-pad display and NOTE/VEL/AT/VOL
  monitor Utility shows. It does not change hardware page or musical routing.
- USB source IDs are saved with Utility's state. `SWAP USB` exchanges physical
  assignments; `RELEARN USB` forgets them for a new controller or MIDI setup.
  These actions release previous gestures. Save the project after assignment.
- `USB ONE NEON` uses the saved unit-1 assignment; use SWAP/RELEARN if only a
  previously assigned unit 2 is connected. `USB TWO NEONS` also works with only
  either one of its assigned units connected, without changing its bank.
- Each surface retains its own performance, slice and stack bank, page,
  selected cell, modifiers, velocity pairing, held gestures and LED destination.
  Sample Neon's single editor follows the most recent press or encoder action;
  another unit's pressure/release or background sync does not steal that focus.
  Performance banks/USB assignments are saved in Utility; the separate editing
  contexts are session-local.
- Musical notes remain standard bank-aware notes for Tracker. Two controllers
  on the same note can retrigger it, but its MIDI gate closes only after the
  final held finger releases. Pressure remains unit-tagged in the control path;
  when both address the same voice, the latest pressure controls that voice.
- Samples, effects, capture and the fill buffer remain shared instrument state.
  Both CENSOR fill holds contribute to the same override; either unit may keep
  it held after the other releases.
- A missing input or input-queue overflow releases that unit's held gestures.
  Recovery requires a fresh press; stale queued presses/velocity are discarded.
  LED output is paired by CoreMIDI entity/destination ID, never display name.
  No fallback sends unit 2's feedback to unit 1 if a device is absent.
- The captured same-packet bank/page restore is ignored in USB mode, retaining
  the current editing page. Intentional later page presses remain functional.
- Since 0.3.2, pads follow the explicitly selected mode/layer, not the page
  encoded in a bank's stale pad addresses. Host MIDI also filters the exact
  adjacent bank + mode-on/off triplet at one timestamp/on one port. PLAY still
  sends the chosen bank's musical notes with measured velocity; other pages
  remain controls. Use Sample Neon 0.40.5 alongside this update. Mode buttons
  select the page for all banks on that unit; they do not affect another unit.

`HOST DUAL PORTS` is an alternative for hosts/routing that provide distinct
CLAP input ports 0 and 1. It preserves unit identity through Tracker but does
not assign hardware LED destinations. REAPER's merged “All MIDI inputs” is not
equivalent; use `USB TWO NEONS` for this Mac setup. Direct USB is macOS-only.
NOTES ONLY still suppresses control/pressure envelopes and therefore does not
provide Sample Neon editing or bank/page LED synchronization.

The original HOST MIDI workflow and nine-byte v1 saved states remain accepted.
The new v2 state stores both banks, input/view choice and USB assignments. The
private v2 bridge adds unit/output identity; Sample Neon still accepts v1.
Tracker's binary/editor need no changes.

### Two-controller bring-up

Do not merge two controllers into the legacy single-controller mapper and
expect independent banks: its bank, page, velocity pairing and held pads are
one controller's state. This limitation motivated the separate USB path above.

The [factory MIDI map](https://www.reloop.com/media/custom/upload/Reloop-NEON_MIDI-Map.pdf)
uses identical primary SAMPLE pad addresses in all four banks. Smart Link's
documented connection notification alone does not identify each pad's sender.
Before adapting bank ownership, capture both units' input and establish a
reliable source identity and a separately addressable LED return path. Never
infer the sender from the most recently received bank button: simultaneous
holds, releases, velocity CCs and aftertouch would become ambiguous.

On macOS the repository includes a **read-only** diagnostic:

```sh
clang++ -std=c++17 -framework Foundation -framework CoreMIDI \
  scripts/neon-midi-monitor.mm -o /tmp/s3g-neon-midi-monitor
/tmp/s3g-neon-midi-monitor
/tmp/s3g-neon-midi-monitor --capture 60
```

It lists only NEON endpoints; capture logs source ID, timestamp and raw packet
bytes. It creates no MIDI output, changes no device properties or host routing,
and stops automatically. While capturing, test each unit separately, clearly
noting which one: SAMPLE, bank A/B, pad 1 press/release, varied velocity and
pressure, then an overlapping hold/release. Existing plugin LED feedback can
change hardware bank/page state; disable NEON OWNER manually for an isolated
input test if needed. No output/LED experiment is performed by this monitor.

If Smart Link merges indistinguishable messages into one endpoint, separate
USB input identities are the next setup to investigate. This is a diagnostic
path, not a claim that dual independent banks or LEDs are already supported.

Separate-USB testing on 2026-09-27 confirmed two distinct CoreMIDI sources,
each paired with its own destination by entity/device identity. Both units
sent the same primary SAMPLE pad-1 bytes, distinguishable by source UID;
overlapping pad gestures also retained their source identities. The diagnostic
now prints entity/device IDs for that pairing. Version 0.2.0 implements separate
input contexts and pairs each LED return through Sample Neon 0.30.0. Physical
LED feedback, held-pad unplug/replug and Tracker recording still require
acceptance in the user's actual REAPER session.

## Implementation and verification

Version 0.1.2 adds only the AT input readout; aftertouch routing, parameter IDs
and the nine-byte state format are unchanged. GUI tests cover independent
note/velocity/AT/VOL updates, both routes, other held pads, bank/base/channel
changes, malformed/late pressure, release, panic, reset, transport stop and
deactivation. The chain with the installed Tracker 0.4.1 and Sample Neon 0.22.2
passes 8,960 checks, including exact aftertouch cell/value/timing forwarding.
Standalone ASan/UBSan: 8,798 checks passed. CLAP validator: 17 passed, zero
failed, four skipped. The installed Utility passed its GUI/chain checks,
bundle comparison and strict signature verification; Tracker and Sample Neon
were not replaced. Previous Utility 0.1.1 is preserved at
`~/Library/Audio/Plug-Ins/CLAP Backups/neon-aftertouch.faPzha/`.
The user's actual REAPER/hardware audition after reload remains separate.

- CLAP ID `org.s3g.s3g-dsp.utility-neon-midi`; installed bundle
  `s3g_utility_neon_midi.clap`. No audio-channel suffix: this Utility has no audio ports.
- Parameters: 1 Bank, 2 Base Note, 3 Output Channel, 4 Control Route,
  5 Release Held Notes (momentary), 6 Input, 7 Unit 2 Bank, 8 Monitor Unit.
  Fixed versioned state stores settings/assignments, never held fingers.
- Allocation-free bounded event processing; SysEx storage lives until process
  returns. No background MIDI heartbeat. Unknown input, status lamps and
  unrelated SysEx are dropped, never sent back to hardware.
- `s3g_utility_neon_midi_clap_smoke`: all banks, velocity/timing, filtering,
  held releases, rejection retry, panic/reset/stop and transactional state.
- `s3g_neon_tracker_chain_smoke`: actual three CLAP binaries in series; notes
  survive REC OFF exactly once and MODE+pad edits the intended bank's cell.
- Tracker core tests cover armed/unarmed monitoring and transparent controls
  and SysEx without recording or audio-thread allocation.

Automated host tests are not a physical REAPER/hardware acceptance test. Check
bank changes, encoder edits, held Grains, LEDs, recording, stop and project
reload in the real host before relying on this chain for a performance.

Initial 0.1.0 verification (before the hardware-velocity diagnosis), 2026-09-25:
13 focused regression suites passed; CLAP validator
reported no failures for all three plugins; the Utility's 265-check standalone
suite passed ASan/UBSan. The installed three-binary chain passed 323 checks,
including notes, modifier edits and an encoder change. Actual hardware/REAPER
acceptance remains pending. Installed Utility 0.1.0, Tracker 0.4.1 and Sample
Neon 0.22.0; previous bundles are in
`~/Library/Audio/Plug-Ins/CLAP Backups/neon-midi-chain.vg56d4/`.

Version 0.1.1 / Sample Neon 0.22.2 verification: 14 focused suites passed.
The corrected three-binary chain plus Tracker's actual recorder core passed
8,713 checks, including recorded normalized volumes in STEP, LIVE Q and LIVE MT.
All 127 strike velocities were replayed as hardware CC + fixed-127 note
sequences on all banks and both routes. Direct Sample Neon PLAY/EDIT renders
match ordinary velocity input and respond dynamically to soft/hard strikes.
GUI tests verify the last-hit readout changes on a strike, not aftertouch.
CLAP validator: 35 passed, 0 failed, 7 skipped across the two updated plugins;
standalone Utility tests passed 8,563 checks under ASan/UBSan. The protocol was
confirmed with live read-only NEON captures; the patched REAPER chain still
needs the user's physical audition after reload. Tracker itself is unchanged.
Installed both updates; the previous Utility 0.1.0 and Sample Neon 0.22.1
bundles are retained in
`~/Library/Audio/Plug-Ins/CLAP Backups/neon-velocity.MSdOQw/`.

References: [Reloop factory MIDI map](https://www.reloop.com/media/custom/upload/Reloop-NEON_MIDI-Map.pdf),
[hardware velocity switch](https://support.serato.com/hc/en-us/articles/10173654019983-Reloop-NEON-Quickstart-Guide),
[CLAP event lifetimes](https://github.com/free-audio/clap/blob/main/include/clap/events.h).
