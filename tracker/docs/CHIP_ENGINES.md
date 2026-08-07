# Chip engine boundary

> Archived research (August 2026): PSG and YMFM are no longer linked into the
> application, listed as available instruments, or routed by the active graph.
> Their sources remain here only as historical experiments.

Classic sound chips are instruments inside the s3g Tracker graph. They do not
define the tracker interface, notation, timing model, or project format. The
tracker continues to use readable MIDI notes, decimal instrument indices,
normalized parameters, typed FX lanes, and sample-domain events.

## Current PSG foundation

The existing `Sn76489PsgNode` is original register-free repository code inspired
by the SN76489 signal topology. The live graph prepares five independent nodes.
A new song exposes one; `+ ADD` assigns another node to the next logical song
index. Every instance owns its oscillator/noise state, base patch, event buffer,
and lock-free parameter mailbox. This bounded-array design is the template for
additional chip families.

## Current YMFM integration and license

[aaronsgiles/ymfm](https://github.com/aaronsgiles/ymfm) is a collection of
Yamaha FM cores covering YM2149, OPM, OPN, OPL, OPQ, and OPZ families. Upstream
uses the [BSD 3-Clause License](https://github.com/aaronsgiles/ymfm/blob/main/LICENSE).
That license is compatible with distributing s3g Tracker source and binaries,
provided source distributions retain the copyright, conditions, and disclaimer;
binary distributions reproduce them in documentation or other included
materials; and Aaron Giles' or contributors' names are not used as endorsements.

The repository vendors only the source needed by the first YM2151/OPM adapter
at revision `81aec25ccbb98f4873a255f7551ac4dadac59b4a`. The exact revision,
upstream README, and license live beside the code in `third_party/ymfm`; the
license is also bundled into the application resources. No ROM/sample data is
included. Any future chip that needs external assets still requires a separate
provenance and redistribution audit.

## Adapter architecture

Each YMFM-backed instrument implements the existing `InstrumentNode`
contract behind an application-owned adapter:

```text
ScheduledEvent
    -> MIDI/patch voice allocator
    -> sample-offset register writes
    -> one private YMFM core instance
    -> chip-rate audio
    -> stateful sample-rate converter
    -> tracker mono/stereo/HOA node bus
```

The adapter, not YMFM, owns:

- MIDI note allocation, stealing, retrigger, release, and tracker note IDs;
- musical patch parameters and their mapping to chip registers;
- exact placement of register writes within an audio callback;
- chip clock selection and stateful conversion from the chip-derived sample
  rate to the current Core Audio rate;
- normalization, output gain, reset, serialization, and telemetry;
- one complete mutable core/register/resampler state per song instance.

No realtime callback may allocate, lock, discover metadata, load ROMs, or rebuild
a resampler. A callback is split at scheduled event offsets, advances the core to
the boundary, applies register writes, and continues. This preserves tracker
microtiming even though the emulated chip has its own clock domain.

## Interface policy

The primary editor should expose musical controls: algorithm, operator ratios,
levels, envelopes, feedback, detune, voice mode, and output. A separate advanced
register inspector can be added for chip specialists, but hexadecimal register
entry is not the sequencing contract. Tracker FX actions use stable semantic
keys and normalized values; raw register addresses remain adapter details.

## First implementation: YM2151 / OPM

The live graph prepares three complete `Ym2151OpmNode` instances. Each owns a
private `ymfm::ym2151`, its eight-channel voice allocation, registers, operator
state, event buffer, base-patch mailbox, and stateful linear chip-rate
converter. Tracker note/parameter events retain sample offsets; the adapter
advances the chip stream to each boundary, applies writes, and continues.

The retained YM2151 OPM circuit window follows active indexed instances. Six
presets seed nine normalized controls for program/operator template, algorithm,
feedback, brightness, attack, decay, sustain, release, and output. The window
shows the selected four-operator routing, snaps algorithm/program/feedback to
valid register positions, and publishes through the same bounded base-patch
path as the audio node. It is a musical circuit editor, not yet a per-operator
register inspector. Adding another YM2151 from the toolbox claims another
complete core; it is not another voice inside the first instance.

The tracker accepts MIDI 0–127 for YM2151. OPM key codes have eight octaves,
so MIDI 12–107 produce distinct pitches; lower and higher values clamp to the
nearest endpoint rather than becoming silent. The editor audition path uses
MIDI 60. PSG, YM2151, and membrane audition requests now share the same bounded
per-node render mailbox instead of bypassing the chip nodes.

## Maintenance status

The chip branch is now in maintenance mode. The delivered PSG and YM2151 remain
available, and correctness fixes such as bounded audition note lifetimes still
apply, but no additional chip core or per-operator editor expansion is on the
active roadmap. The native stereo slice sampler and dedicated DaisySP-informed
drum/effect work described in [DRUM_ENGINE_RESEARCH.md](DRUM_ENGINE_RESEARCH.md)
take priority. OPL3, OPNB, OPN2, and SSG remain optional future color modules
only if a later listening test identifies a specific gap.

If chip development resumes, each core still arrives as a distinct available
instrument type with multiple independent instances, rather than as a mode
switch inside the SN76489-style PSG.
