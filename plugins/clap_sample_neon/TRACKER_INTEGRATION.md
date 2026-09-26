# Sample Neon 32 + s3g Tracker

Recommended division: **Tracker composes timing; Neon creates and performs sound**.
Keep patterns, probability, ratchets, polymeter, Bursts and Song structure in
Tracker. Avoid adding a second pattern sequencer inside Neon.

The single-track route below adds the Utility Neon MIDI adapter, Tracker 0.4.1
MIDI thru, and Sample Neon 0.22.0 control-envelope input. It is covered by an
automated three-CLAP host test, not a completed physical REAPER acceptance test.
The longer-term proposals below remain separate work.

## Hardware recording on one track

Use **Utility Neon MIDI → Tracker → Sample Neon 32** in the normal FX chain.
The Utility's `TRACKER + SAMPLE NEON` route produces clean musical notes and
separate private control envelopes. Tracker records the notes, forwards controls,
and now monitors live notes even with `REC OFF`. Editing/secondary pads do not
become recorded notes or duplicate primary SAMPLE triggers.

Keep Base Note 36 in Utility and Neon, output/recording lanes CH01, and Sample
Neon OWNER on. Hardware input must retain its original channels. Do not add a
parallel raw-input send or a hardware return send. Select performance banks on
the hardware or Utility GUI: that bank is authoritative downstream. Sample
Neon's GUI cannot send bank changes upstream; CHOP has an independent slice bank.
See the [Utility setup and limitations](../clap_utility_neon_midi/README.md).

## Current connection

Tracker emits sample-offset MIDI notes, velocity, note-offs and CC messages.
Neon's ordinary note input already addresses its 32 cells independently of the
visible bank/page. At the default **Base Note = 36**:

| Cells | MIDI note numbers |
| --- | --- |
| A1–A8 | 36–43 |
| B1–B8 | 44–51 |
| C1–C8 | 52–59 |
| D1–D8 | 60–67 |

Use MIDI numbers when configuring the pair: octave-name conventions can differ.
Changing pitch in Tracker currently selects a different cell; it does not play
the selected cell chromatically. Neon's cell Tune parameter remains separate.

For simultaneous hardware use, the clearest starting routing is two tracks:

1. Tracker track: send MIDI to the Neon instrument track, with all participating
   Tracker lanes set to **CH01**. Do not send Tracker MIDI to the physical NEON.
2. Neon instrument track: receive that send and the physical NEON input on its
   native channels, with monitoring enabled. Keep **NEON OWNER** on in this one
   instance. Do not remap the physical controller's channels to CH01.
3. Optionally set Neon's **Standard MIDI Receive** host parameter to channel 1;
   physical NEON decoding is separate and still uses its native messages.
4. On Mac, leave hardware MIDI sends back to NEON disabled; LED output is direct.
   Preserve the chosen audio bus widths and ACN/SN3D channel order downstream.

With no physical controller involved, Tracker can simply precede Neon in one
FX chain. Routing raw controller messages through Tracker's recording input is
not a substitute for decoding them: Tracker's armed-input monitoring remaps
incoming notes to the armed lane's output channel.

**Important limitation:** while NEON OWNER is enabled, raw MIDI on channels 4–12
can overlap controller commands/status addresses before ordinary note filtering.
CH01 avoids this collision for the current pairing. Changing Standard MIDI
Receive alone does not make arbitrary sequencer channels safe.

Use **One Shot** for independently decaying hits. Use **Hold** when Tracker's
note length/GATE should control release, especially for Motion and Grains.
Toggle is a performance latch, not a note-length gate. Start with Free/Pad clocks
when auditioning from Tracker with REAPER stopped; choose Host deliberately when
transport should pause the generated playback.

## Recommended next work, in order

1. **Sequencer-safe input and stopping.** Explicitly distinguish performance
   controls from musical MIDI, then test held notes, same-note retriggers,
   transport stops/seeks/loops and panic together. Tracker currently releases
   its active notes and sends CC123 on all channels for panic; Neon does not yet
   implement generic CC120/123 handling. One Shot and Toggle need an explicit
   stop policy, rather than assuming note-off will stop every technique.
   Separate CLAP note inputs are one candidate, subject to REAPER routing tests;
   the [CLAP note-port interface](https://github.com/free-audio/clap/blob/main/include/clap/ext/note-ports.h)
   provides port descriptions but does not establish host UI support by itself.
2. **Explicit per-cell sound control.** A documented MIDI/automation map for
   tune, sample position, cutoff, FX amount, grain density/size and motion rate.
   Tracker already has stepped and interpolated CC lanes, but Neon currently
   does not map ordinary CCs to those sound controls. Targets must be stable
   cells/macros, not whichever pad happens to be selected in the GUI. Expose
   the relevant currently-internal technique parameters and define reset/recall
   behavior before calling this per-step parameter locking.
3. **Bar-length resampling.** Arm at the next bar and record 1/2/4/8 bars of a
   Tracker passage, then crop/assign using the existing linked-channel capture
   path. Make fixed-length capture visibly different from manual REC toggle.
4. **A pairing preset and edit safety.** A Tracker lane/note-map template for
   Neon and optional MIDI-number pad labels; then undo/redo for source edits,
   slice assignment and normalization. These improve repeatability without
   adding more overlapping playback modes.

Current implementation references:

- [Tracker guide](../../docs/s3g-tracker.html): note output, GATE and CC interpolation.
- [Tracker MIDI engine](../../tracker/src/core/clap_midi_engine.cpp): scheduling and panic.
- [Neon MIDI bridge](s3g_sample_neon_clap.cpp): controller-first decoding and standard note routing.

## Combined acceptance before a sequencing release

Exercise all 32 notes with the visible bank changed; velocity accents, ratchets
and releases in all playback techniques; simultaneous physical pads and Tracker;
stop/seek/loop/panic with long grain releases; project reload; deterministic CC
targeting; stereo/quad/octo/ACN-SN3D recording and playback. Inspect hardware LED
behavior alongside audio, and verify there is no MIDI return route or control
message interpreted as a musical note.
