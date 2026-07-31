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

The No Input Mixer application is emitted as
`build-apps/apps/no_input_mixer_standalone/s3g No Input Mixer.app`.

## Safety and lifecycle rules

- Standalone instruments launch monitoring-muted.
- The audio callback performs no allocation, device enumeration, state I/O, or
  GUI work.
- Device changes stop AUHAL before processor reactivation.
- Unused hardware channels are explicitly cleared.
- Processor state is saved and loaded only through `CLAP_EXT_STATE`.
- Core Audio device identity is persisted by UID rather than `AudioDeviceID`.

## Follow-up infrastructure

The next shared additions should be device hot-plug listeners, CoreMIDI output
feedback, a versioned document format for explicit Save/Load, and
signing/notarization helpers. Once No Input Mixer and Multi Loop both use this
layer, the common assumptions can be reduced into a higher-level CMake product
declaration.
