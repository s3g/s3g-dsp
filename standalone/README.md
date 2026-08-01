# s3g standalone host layer

This directory contains the reusable host infrastructure for standalone
applications built from s3g CLAP processors. Product-specific application
targets remain under `apps/`.

## Architecture

Standalone products host the same CLAP implementation used by the normal
plugin bundle. They do not construct a second copy of the processor model or
call private plugin state directly.

The first product chain is:

```text
NIM Gesture CLAP (MIDI control and explicit performance sessions)
  ->
No Input Mixer CLAP (8 channels)
  -> Stereo Autogain CLAP (2 channels), or
  -> Quad Autogain CLAP (4 channels), or
  -> direct eight-channel output
  -> AUHAL/Core Audio device
```

Each output mode can target any aligned, contiguous hardware bank exposed by
the selected device: pairs for stereo, groups of four for quad, and groups of
eight for direct output. The application remembers a separate bank for each
mode.

`host/s3g_embedded_clap_host.*` owns CLAP lifecycle, public extensions, state
streams, and thread-safe host requests. `audio/s3g_coreaudio_output.*` owns
device discovery and AUHAL output. Neither layer contains product parameters or
GUI knowledge.

The normal plugin source declares `clap_entry`. An application may compile the
same source with `S3G_CLAP_ENTRY_SYMBOL` set to a product-unique symbol, allowing
multiple processors to be linked into one application without exported-symbol
collisions.

## Adding another product

1. Add an application directory under `apps/`.
2. Embed its source CLAP with a unique entry symbol.
3. Keep its render chain in a C++ engine that can be tested without Cocoa.
4. Use `EmbeddedClapPlugin` for activation, processing, state, and GUI access.
5. Use `CoreAudioOutput` for device output rather than duplicating AUHAL code.
6. Add a headless smoke target covering channel order, finite output, unused
   channel clearing, safety mute, and CLAP state round trips.

## Building

```sh
cmake --preset apps
cmake --build build-apps --target s3g_no_input_mixer_app
cmake --build build-apps --target audit_no_input_mixer_standalone
```

The `apps` preset is an optimized Release configuration; real-time standalone
products must not be shipped from an unoptimized compiler-default build.

The No Input Mixer application is emitted as
`build-apps/apps/no_input_mixer_standalone/s3g No Input Mixer.app`.

## Safety and lifecycle rules

- Standalone instruments launch monitoring-muted.
- The audio callback performs no allocation, device enumeration, state I/O, or
  GUI work.
- Device changes stop AUHAL before processor reactivation.
- Unused hardware channels are explicitly cleared.
- Automatic processor state is saved and loaded through `CLAP_EXT_STATE`.
  Product-specific explicit files use a versioned plugin extension rather than
  exposing or duplicating the processor's internal model in the application.
- Core Audio device identity is persisted by UID rather than `AudioDeviceID`.
- The persistent status strip reports smoothed and peak callback load, maximum
  callback time, deadline misses, HAL overloads, oversized callbacks, callback
  errors, and MIDI input queue drops.
- MIDI input reaches the embedded processors through bounded real-time queues.
  CoreMIDI host timestamps are retained, converted to CLAP sample offsets, and
  future events are deferred to the correct audio block. CoreMIDI transmission
  is drained on the main thread.
- The MIDI panel enumerates CoreMIDI input sources independently, supports
  Enable All/Disable All, remembers checked sources by unique ID, and rescans
  when reopened. The CLAP remains host-routed and never opens hardware ports.
- Controller feedback is disabled until the user chooses destinations. No
  Input Mixer exposes separate NRPN and Grid destinations so encoder-ring
  feedback and signed BU16 matrix LEDs can operate simultaneously.
- Embedded NIM Gesture recordings are transient. The application starts with
  no loops and Play paused, omits Gesture from automatic preferences, and
  removes preferences written by older builds. Users retain a performance only
  by explicitly saving a versioned `.nimgesture` file; loading replaces the
  current loops and remains paused until Play is deliberately enabled.
- The embedded processor exposes the same `BU16 FLIP` and `BU16 LATCH` modes
  as the CLAP: pressure-morph an existing point across polarity, or create,
  pressure-set, latch, and toggle signed performance points. NRPN parameter 58
  selects the sign used for new latches. The shared `BU16 Ramp` at NRPN 59
  fades both modes from 20 to 10000 ms and defaults to 1000 ms.

## Follow-up infrastructure

The next shared additions should be device hot-plug listeners and
signing/notarization helpers. Once No Input Mixer and Multi Loop both use this
layer, the common assumptions can be reduced into a higher-level CMake product
declaration.
