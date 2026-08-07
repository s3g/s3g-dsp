# ADR 0001: CLAP is a module boundary, not the tracker core

- Status: Accepted
- Date: 2026-08-05

## Context

`s3g-dsp` has proven that multiple CLAP processors can be linked into a native
application with renamed entry symbols and driven through a small embedded
host. The tracker also needs a reusable path to future drum synthesis, sample
processing, stereo/quad folds, and a possible DAW product.

CLAP is a host/plugin ABI. Its official contracts cover processor lifecycle,
audio processing, transport, parameters, state, note/MIDI events, audio/note
ports, latency, and GUI attachment. It does not define tracker cells,
polymetric phases, song forms, commands, undo, controller pages, project files,
or application graph ownership. See the official [CLAP repository and
overview](https://github.com/free-audio/clap), [process
contract](https://github.com/free-audio/clap/blob/main/include/clap/process.h),
and [event model](https://github.com/free-audio/clap/blob/main/include/clap/events.h).

## Decision

Use CLAP for:

- Trusted hosted `s3g-dsp` processors whose public state/editor/port boundary
  is useful.
- New reusable DSP products built as a plain DSP core plus a CLAP adapter.
- A later thin `s3g Tracker.clap` wrapper for DAW use.

Do not use CLAP for:

- The tracker document, scheduler, transport ownership, commands, undo, or
  controller API.
- A mandatory ABI between small internal mixer/voice primitives.
- Hosting Superior Drummer in the MIDI-first product.

## Consequences

The standalone application is the host and owns its clock. Its pure C++ core
emits canonical sample-time events. Adapters translate those events to
timestamped CoreMIDI or CLAP note/parameter events. Native DSP and embedded
CLAP nodes can coexist behind one audio-graph interface.

The fixed membrane rack initializes one renamed compiled-in CLAP entry session
and creates five independent plugin instances beneath it. Instance destruction
is completed before the shared entry is deinitialized. This shares the entry
lifetime, not parameter, voice, state, or tail data.

The rack uses a tracker-native Cocoa base-patch editor rather than attaching
the membrane plugin's own view. That choice keeps a single normalized model
for the five-node kick capacity, prevents topology controls such as Output Format from
invalidating the prepared graph, and leaves sample-timed FX automation in the
tracker. It does not change the decision to reuse the membrane DSP and state
boundary through CLAP.

The existing `EmbeddedClapPlugin` is suitable for a curated initial chain but
must gain complete note/audio port discovery, latency changes, rescan/restart,
thread checking, logging, parameter flushing, and sleep/wake handling before
it becomes a general graph host. Arbitrary third-party dynamic scanning is out
of scope.

Its current convenience `activate()` also combines CLAP control-thread
activation with render-thread `start_processing`. The tracker membrane adapter
uses the host for create/state but invokes activate/deactivate and
start/stop-processing as separate lifecycle phases. `s3g-dsp` should export
that split directly before the next standalone graph reuses the helper.

The `s3g-dsp` tree currently pins CLAP 1.2.6. Version changes must be
coordinated rather than allowing the tracker to fetch a second independently
versioned copy. Validator, state, and real-time tests gate any bump.

## Alternatives rejected

- **Tracker entirely inside a CLAP plugin:** reverses ownership for the primary
  standalone product and provides no useful abstraction for documents or
  controllers.
- **Every internal node is CLAP:** adds lifecycle and event translation costs
  without helping small private primitives.
- **No CLAP in the application:** gives up tested editors/state and duplicates
  wrappers around reusable `s3g-dsp` products.
- **Host Superior Drummer immediately:** expands v1 into AU/VST3 hosting when a
  timestamped virtual/selectable CoreMIDI route solves the stated goal.
