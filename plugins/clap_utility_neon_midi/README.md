# s3g Utility Neon MIDI

Version 0.1.1. MIDI-only CLAP companion for a Reloop NEON. No firmware change,
audio processing, hardware MIDI output or second LED owner. Uses the same
factory-message decoder as Sample Neon 32. Utility-family Fira Code/grayscale
editor: 10 pt body, 15 px menu fields, 18 px dropdown rows, 65–200% resizing.

## One-track REAPER setup

Place these **in this order in the normal track FX chain**:

1. **s3g Utility Neon MIDI 0.1.1** — Control Route `TRACKER + SAMPLE NEON`,
   Base Note `36`, Output Channel `1`.
2. **s3g Tracker 0.4.1 or newer** — participating lanes, including the recording
   destination, on `CH01`.
3. **s3g Sample Neon 32 0.22.2 or newer** — Base Note `36`, Standard MIDI Receive
   `1`, and `NEON OWNER: ON` in this one instrument instance.

Choose the physical NEON as the track's MIDI input, on all its native channels;
enable input monitoring (and arm the track if required by the REAPER setup).
Do not remap input channels before the Utility. No parallel raw-NEON send
to the instrument: that would duplicate the controller path. On Mac, **no MIDI
hardware send back to NEON**; Sample Neon owns direct CoreMIDI LED output.
The Utility does not open the MIDI device itself.

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
separate slice bank never changes the performance bank. This is one-way
synchronization, not automatic bidirectional linking.

## Controls and recording are separate

The factory SAMPLE pads send the same raw channel-8 notes 0–7 in every bank.
The adapter remembers bank buttons and emits `base + bank*8 + pad` instead.
Velocity and sample offsets are retained. Releases latch the original note
and channel even if bank, page, SHIFT or mapping changes under a held finger.

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

The footer displays the last successfully emitted **NOTE**, **VEL** (1–127)
and **VOL** (`VEL / 127`, 0–1): e.g. `NOTE 46 / VEL 003 / VOL 0.024`.
It holds that strike value through aftertouch and release; the adjacent
SHIFT + SAMPLER reminder is not a claim to know the hardware's current mode.

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
Then control envelopes are suppressed. There is no raw MIDI-thru in the
Utility, and it deliberately does not accept a keyboard or already-translated
notes as an additional input. Connect those separately after the adapter.

`RELEASE HELD` sends note-offs for active musical input. Host reset/transport
stop also releases held notes; output-rejected releases are retried. This is
not an instrument-wide kill for One Shot/Toggle or editor audition; Sample
Neon's KILL remains available. Change base/channel with no keys held and keep
the destination's map matched. Default CH01 avoids Neon's native command
channels. Each Utility instance handles **one controller**; merged two-NEON
input and Smart Link are not implemented.

## Implementation and verification

- CLAP ID `org.s3g.s3g-dsp.utility-neon-midi`; installed bundle
  `s3g_utility_neon_midi.clap`. No audio-channel suffix: this Utility has no audio ports.
- Parameters: 1 Bank, 2 Base Note, 3 Output Channel, 4 Control Route,
  5 Release Held Notes (momentary). Fixed versioned state stores only settings.
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
