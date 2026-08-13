# s3g-dsp

A pre-release collection of CLAP audio plugins for multichannel and ambisonic
work in REAPER.

The packaged release supports Apple silicon (`arm64`) Macs. Other platforms,
Intel Macs, DAWs, and plugin formats are not currently supported. Plugin names,
parameters, and saved-state formats may change before a stable release.

Full documentation, plugin guides, routing notes, and build instructions:
<https://s3g.github.io/s3g-dsp/>.

## Install

The packaged pre-release supports REAPER on Apple silicon (`arm64`) Macs. It is
not notarized, so macOS requires one Gatekeeper approval for the installer.

1. Quit REAPER, download the macOS CLAP zip from the
   [GitHub releases page](https://github.com/s3g/s3g-dsp/releases), and unzip
   it.
2. Double-click `Install s3g-dsp CLAPs.command` in the unzipped package.
3. If macOS blocks it, click **OK**, open **System Settings > Privacy &
   Security**, and choose **Open Anyway**. Confirm **Open** and authenticate if
   asked. This option is available for about one hour after the blocked launch.
4. If prompted, allow Terminal to access the downloaded package, then wait for
   installation to finish.
5. Start REAPER and rescan CLAP plugins if the new builds do not appear
   automatically.

See Apple's
[Open a Mac app from an unknown developer](https://support.apple.com/guide/mac-help/mh40616/mac)
for the same approval process. If a binary is unavailable, see
[Building From Source](https://s3g.github.io/s3g-dsp/building-from-source.html).

### Install Location and Safety

```text
~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/
```

On upgrades, the installer moves identity-verified older s3g-dsp
bundles—including copies in the former top-level CLAP location—to a timestamped
backup. Other CLAP products are untouched.

The installer verifies each bundle, removes only `com.apple.quarantine` from
its staged s3g-dsp copies, and preserves other extended attributes. It writes
to the current user's Library without `sudo` and does not change global
Gatekeeper settings or unrelated plugins. Approval applies to the installer;
REAPER should not show a separate Gatekeeper dialog for each plugin afterward.

### Manual Installation

Use this only for a fresh manual install; it cannot migrate older top-level
copies or retire renamed aliases. In Terminal, type `cd` followed by a space,
drag the unzipped `s3g-dsp-macos-clap-0.7.0-pre` folder from Finder into the
Terminal window, and press Return. Then run:

```sh
destination="$HOME/Library/Audio/Plug-Ins/CLAP/s3g-dsp"
mkdir -p "$destination"
for bundle in ./*.clap; do
  installed="$destination/$(basename "$bundle")"
  ditto "$bundle" "$installed"
  xattr -drs com.apple.quarantine "$installed"
done
```

### If a Plugin Does Not Appear

- Confirm that its complete `.clap` bundle is in
  `~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/`.
- Rescan CLAP plugins in REAPER, then restart the host.
- If REAPER shows per-plugin Gatekeeper dialogs or duplicate names, quit REAPER
  and rerun the packaged installer. If the installer itself is blocked, repeat
  the **Open Anyway** step within one hour.

For more detail, see the
[installation guide](https://s3g.github.io/s3g-dsp/installing-plugins.html).

The optional No Input Mixer standalone application is distributed in its own
ZIP with a user-level installer and does not require REAPER or the CLAP archive.
See [Standalone Apps](https://s3g.github.io/s3g-dsp/apps.html) and the
[No Input Mixer Standalone guide](https://s3g.github.io/s3g-dsp/no-input-mixer-standalone.html).

## Build From Source

See [Building From Source](https://s3g.github.io/s3g-dsp/building-from-source.html)
for requirements, CMake presets, optional dependencies, validation, packaging,
and local installation.

The included [s3g Tracker CLAP](https://s3g.github.io/s3g-dsp/s3g-tracker.html)
is a host-synchronized MIDI tracker for REAPER and a companion to the s3g drum
synthesis plugins. Repository-level implementation notes remain in
[the tracker document](docs/s3g-tracker-clap.md).

The included [s3g Slicer design and build notes](docs/s3g-breakbeat-slicer-design.md)
describe the shared four-break stereo/16-channel sampler CLAP that pairs with
Tracker through ordinary MIDI. Its stereo and fixed 16-channel variants share
the same overview/editor and real-time engine.

The [s3g Processor Formant Matrix documentation](https://s3g.github.io/s3g-dsp/formant-matrix.html) describes the
22-band CLAP effect vocoder and resonant routing matrix, selectable external
mic/internal speech modulators, four/eight-pole analysis, and a sample-free
carrier driven either by polyphonic MIDI or monophonic scale-quantized voice
pitch. It is advertised to hosts as a stereo audio effect/filter while
retaining MIDI note input.

The included [Processor Feedback Shift 0.18.1](https://s3g.github.io/s3g-dsp/processor-feedback-shift.html)
is an eight-channel CLAP audio effect built around paired feedback scenes,
high-density virtual patch splicing, ecology-responsive governors, per-node
processor inserts, an in-network aux return, and post-network granulation. It
ships in the 0.7 CLAP archive.

[Processor Fissure](https://s3g.github.io/s3g-dsp/processor-fissure.html) is a
physical compositional noise instrument and optional-input processor. One fixed
eight-channel CLAP hosts its contact/shaker excitation, eight independently
authored physical objects, metered mic-to-contact transfer, twelve complete
factory instruments, a paintable signed 8-by-8 matrix, six topology grammars,
full-instrument scene morphing, scoped performance gestures, MIDI strikes, and
switchable Stereo, Quad, or 8 Direct projection.

## Related Projects

- [`s3g-mc`](https://github.com/s3g/s3g-mc): REAPER scripts, JSFX, and
  multichannel workflow tools
- [`s3g-max`](https://github.com/s3g/s3g-max): Max/MSP externals and packages
- [`s3g-clap-max`](https://github.com/s3g/s3g-clap-max): a Max object for using
  the CLAP plugins in `s3g-dsp` with Max
- [`s3g-rnbo-clap`](https://github.com/s3g/s3g-rnbo-clap): CLAP wrappers for
  RNBO-generated DSP

## License and Citation

The repository is BSD-3-Clause unless a subdirectory states otherwise. See
[`LICENSE`](LICENSE), [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and
[`CITATION.cff`](CITATION.cff).
