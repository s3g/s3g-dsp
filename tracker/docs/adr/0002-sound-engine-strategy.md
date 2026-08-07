# ADR 0002: Hybrid native graph with curated sound modules

- Status: Accepted, amended to archive chip instruments
- Date: 2026-08-06

## Context

The tracker needs some immediate internal sound, later sample playback and
modal drums, and eventually a useful selection of classic chip voices. It must
also retain first-class timestamped MIDI output for Superior Drummer and other
external instruments. `s3g-dsp` already provides reusable DSP products and a
small embedded CLAP host, while a tracker has stricter ownership and routing
needs than a chain of plugins.

Furnace is a valuable behavior and coverage reference for chip synthesis, but
the Furnace application is GPL-covered and its individual emulator cores have
their own provenance and license histories. No Furnace implementation, ROM,
table, preset, or other asset is imported by this project. Every prospective
chip core must be audited at the exact source revision, including transitive
code and required data, before an implementation choice is made.

## Decision

Use a hybrid graph:

- The tracker owns the authoritative sample clock, canonical event stream,
  routing graph, and bounded voice/event storage. It will also own the sample
  pool, mixer, and device-layout policy. The first native stereo slice node now
  establishes immutable sample ownership and bounded render-time voices.
- External MIDI remains a peer destination, not a fallback mode.
- Reusable `s3g-dsp` instruments may be embedded through CLAP when their
  existing parameter, state, and editor boundary is valuable. Small private
  graph primitives remain native.
- A tracker-native editor may sit above a curated CLAP instrument when the app
  needs one normalized multi-instance model, must hide prepared-topology
  controls, or needs tighter integration with lanes and performance actions.
  Reusing DSP through CLAP does not require embedding that plugin's GUI.
- New reusable drum voices should expose a plain DSP core in `s3g-dsp` with a
  CLAP product wrapper. The tracker can select the wrapper or a deliberately
  exported core without duplicating the algorithm.
- DaisySP is the preferred modern source/reference library for the next
  dedicated drum and effect prototypes. Modules are selected and pinned rather
  than importing the library wholesale, and remain behind tracker-owned event,
  parameter, voice, and routing adapters.
- Any future classic chip engine would have to sit behind the same
  `InstrumentNode` and canonical event boundary after a written license,
  fidelity, CPU, state, reset, sample-rate, and redistribution audit. The PSG
  and YM2151 experiments are now archived and excluded from the active build.

The first reusable module is the existing `s3g-dsp` membrane-kick CLAP. The
optional live build prepares capacity for five independent copies under one
shared compiled-entry lifetime. The song starts with one `MEMBRANE KICK` and
exposes another copy only when the user adds an instance. These are not snare or
tom roles: each future drum type remains separate DSP work.

The indexed default song is `00` MEMBRANE KICK, `01 STEREO SLICE SAMPLER`,
and `02 MIDI OUT`. The active type library also contains the five selected
DaisySP drum voices. Adding another instance assigns the next free song index
and one unused engine node. Each track selects a default index while its polymetric
INS column can override the choice per row. Routing is derived from the
resolved instrument kind. MIDI OUT is therefore a normal instrument rather
than a parallel track flag; future simultaneous internal+MIDI layering must be
an explicit stack instrument.

## Consequences

The architecture permits internal synthesis, samples, and MIDI to share
one pattern and scheduling contract. The implemented device slice combines the
hybrid rack with selectable live stereo output; quad decode, per-slot state
isolation, simultaneous summing, and eventless tails are covered headlessly.
The native sampler render core, indexed rack integration, AudioToolbox loader,
waveform/slice editor, and live stereo graph path are delivered; background
loading, transient/manual slicing, dedicated tom/metal algorithms, and live
quad/multichannel layouts remain future work.
CLAP reuse does not force the tracker model or editor into a plugin ABI, and
native voices do not require an unnecessary wrapper.

The project will not copy convenient chip code first and resolve licensing
later. If a desired implementation cannot be redistributed under the chosen
product terms, it must be replaced with a compatible upstream core or an
independent implementation before integration. This ADR is an engineering
policy, not a substitute for legal review of a release dependency set.
