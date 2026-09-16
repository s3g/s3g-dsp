# s3g-dsp

A pre-release collection of CLAP audio plugins for multichannel and ambisonic
work in REAPER.

The `0.10.0-pre` packages target REAPER on Apple silicon (`arm64`) Macs and
**experimental Windows x64**. Windows testing has used Windows 10; Windows 11
is not yet validated. Intel Macs, native Windows ARM64, other DAWs, and other
plugin formats are not validated targets. Plugin names, parameters, and
saved-state formats may change before a stable release.

Full documentation, plugin guides, routing notes, and build instructions:
<https://s3g.github.io/s3g-dsp/>.

## Install on Mac

The packaged pre-release targets REAPER on Apple silicon (`arm64`) Macs with
macOS 15.0 or newer (tested on 15.7.4). It is not notarized, so macOS requires
one Gatekeeper approval for the installer.

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

Back up your current plug-ins before upgrading. The installer replaces current
canonical bundles and moves verified renamed/retired aliases—including copies
in the former top-level CLAP location—to a timestamped backup. Other CLAP
products are untouched.

The installer verifies each bundle, removes only `com.apple.quarantine` from
its staged s3g-dsp copies, and preserves other extended attributes. It writes
to the current user's Library without `sudo` and does not change global
Gatekeeper settings or unrelated plugins. Approval applies to the installer;
REAPER should not show a separate Gatekeeper dialog for each plugin afterward.

### Manual Installation

Use this only for a fresh manual install; it cannot migrate older top-level
copies or retire renamed aliases. In Terminal, type `cd` followed by a space,
drag the unzipped `s3g-dsp-macos-clap-0.10.0-pre` folder from Finder into the
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

## Install on Windows (experimental)

1. Quit REAPER and extract the complete
   `s3g-dsp-windows-x64-clap-0.10.0-pre-experimental.zip` to a fresh folder.
2. Add that folder to REAPER's CLAP search paths and rescan. Keep the `.clap`
   files beside the shared `Resources` folder, including fonts and licenses.
   No system font installation or Mac installer is needed.
3. Keep older packages outside the scan paths as backups to avoid duplicate
   CLAP identifiers. Test with a disposable project and conservative levels.

Mac includes 122 bundles / 129 plug-in descriptors; Windows includes 121 files /
128 descriptors. **Ambi Energy remains Mac-only.** Heavy ambisonic configurations
can overload older CPUs; the tested i7-4510U / 8 GB laptop is not a recommended
minimum specification. See the packaged README and [release notes](RELEASE_NOTES.md).

Current release downloads contain the CLAP collection only. Experimental standalone
application targets remain available in the source tree but are not packaged
for this release; Tracker's standalone app has been retired.

## Build From Source

See [Building From Source](https://s3g.github.io/s3g-dsp/building-from-source.html)
for requirements, CMake presets, optional dependencies, validation, packaging,
and local installation.

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
