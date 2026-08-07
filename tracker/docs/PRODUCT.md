# Product sketch

## North star

Make polymeter feel playable rather than configured: every lane and every
field can have its own loop length, stride, and direction, while edits remain
legible enough to perform without stopping.

The first product fit is a tight drum tracker for Superior Drummer. It should
be faster to create, disturb, and recover a polymetric groove from hardware
than it is from a DAW piano roll.

## Primary performance surface

```text
┌ transport / tempo / swing / device status / module windows ────────────┐
├ live code  : [command entry / history] ─────────────────────────────────┤
│ row │ LANE 1 NOTE/INS/VOL │ LANE 2 NOTE/INS/VOL │ ... │ INSTRUMENTS    │
│ 00  │ C-2   I00    .92    │ D-2   I02    .88    │     │ 00 MEMBRANE    │
│ 01  │ ---   ---    PRV    │ ---   ---    PRV    │     │ 01 SAMPLER     │
│ 02  │ G-2   I01    .81    │ ---   ---    PRV    │     │ 02 MIDI OUT    │
│ ... │ independent NOTE, INS, VOL, and FX playheads   │ AVAILABLE +ADD │
├ selected-lane normalized volume envelope ───────────┤ TRACKER ZOOM    │
│                                                     │ CONSOLE OUTPUT  │
├ selected instrument Device View / device chain / route or preset ──────┤
└───────────────────────────────────────────────────────────────────────────┘
```

The tracker grid stays dominant. Live-code entry and command history sit with
transport, while printed commands, results, status, completion matches, and
errors appear in a dedicated scrollable readout under the right instrument
toolbox. The selected-lane Volume Envelope shares the central area with that
persistent right-side toolbox. Rhythm Geometry
is a retained pop-out so its pulse polygons can grow without narrowing the
core editing surface. All three are jsui-inspired native views over one pattern
model. Song form belongs in a retained alternate window. Larger transforms can
become modes or drawers as their interaction design matures.

The full-width bottom Device View follows the selected song instrument. It is
the consistent home for an instrument's preset or route controls and the
visible chain toward MAIN OUT. The first two effect positions are explicit
empty reservations until real graph devices, state, and latency exist.

A main-workspace mixer page can replace the grid/envelope area without hiding
transport entry or the console-output toolbox. Its first performance slice treats tracks as
sequence/event sources: velocity-input trim, NOTE mute/solo, instrument, and
step activity. A separate MAIN OUT controls post-decode internal audio and
shows its aggregate peak. Per-track post-DSP faders, pan, sends, and meters
wait for explicit lane render buses rather than being simulated with velocity.

Tracks are rhythmic/polyphonic channels with an indexed default instrument.
Adding tracks up to the bounded 32-track limit is the normal way to add voices.
Readable per-row `INS` cells remain an optional override before FX and onset
resolution. MIDI note pitch remains independent, preserving the Superior
Drummer workflow while one track can deliberately move among internal drums,
later samples, chip voices, or MIDI OUT.

## Controller roles

Controllers emit semantic actions into one command engine. No device is
allowed to mutate tracker memory directly, and CLAP parameter IDs never become
the controller contract.

| Device | First role | Feedback |
| --- | --- | --- |
| BU16 | Sixteen steps for the selected lane; pressure sets normalized VOL; bank to NOTE/INS/VOL/FX fields | Diffed step, playhead, selection, mute, instrument role, and pending-launch colors |
| OXI E16 | Lane and field length/stride/direction, instrument choice, velocity/FX values, generation amount, tempo/swing | 14-bit NRPN ring state and page labels |
| Keychron Q0 Max | Transport, cursor, enter/rest/retrigger, lane select, snapshot/launch, panic | Dedicated MIDI firmware LEDs after the mapping stabilizes |
| Computer keyboard | Complete cell editing and command search | On-screen selection and command state |

Use persistent CoreMIDI unique IDs, hot-plug recovery, reconnect snapshots,
loop suppression, and separate performance/feedback queues. The Keychron can
act as a normal keyboard during early development; typed live-console macros
are not the finished integration.

## Superior Drummer first

Version one exposes indexed MIDI OUT instruments, each owning one of eight
separately published `s3g Tracker N` virtual sources or a selectable physical
destination and channel. It does not host
Superior Drummer. It ships an editable
Superior articulation map seeded with useful, familiar defaults.

Required basics:

- Per-MIDI-device channel plus per-lane note/articulation mapping.
- Configurable gate time, choke groups, and overlap-safe note-offs.
- Timestamped note scheduling, destination latency offset, panic, and stop
  cleanup.
- A separate low-priority path for controller LEDs and encoder rings.

## Internal audio and voice architecture

The first internal instrument is one indexed instance of the good `s3g-dsp`
membrane Kick CLAP. The engine prepares capacity for four additional instances;
adding one from the type library creates a new song index with independent
tail and patch state. Instances sum their 16-channel ACN/SN3D output and decode
once. They are not relabelled as provisional snare/tom instruments. The next
default song entry is a native stereo slice sampler. Up to three independent
indexed instances can be added; each retains its own immutable decoded asset,
128-slice map, base note, reverse settings, and 32 playback voices. Five
DaisySP drum models provide three independent instances each. The standalone opens a
user-selected stereo output; stereo and quad engine layouts are covered by
headless tests for later expansion.

Per-row instrument memory addresses those slots without binding a lane to one
drum. The same typed column is the insertion point for later modal and
metal/noise nodes.

The next drum voices are dedicated tom and metal/noise hat/cymbal DSP
instruments rather than membrane-role presets. The sampler's next pass moves
decoding off the UI thread and adds transient/manual slicing. Voices must render
into explicit mono, stereo, quad, discrete, or ambisonic buses; the master
device layout is never assumed to be stereo.

New reusable synthesis voices live as plain DSP cores with CLAP wrappers in
`s3g-dsp`; the tracker can host the CLAP wrapper when its state boundary is
useful or call an intentionally shared DSP target when it is not. Tracker-owned
native editors remain free to present curated musical roles instead of a
plugin's generic parameter view.

## Deliberately deferred

- Arbitrary third-party plugin scanning/hosting.
- Reimplementing every Max panel before MIDI timing works.
- Cross-platform GUI/audio abstraction.
- DAW synchronization and a tracker CLAP product.
- Offline bounce and hosting Superior Drummer's AU/VST3 inside the app.
