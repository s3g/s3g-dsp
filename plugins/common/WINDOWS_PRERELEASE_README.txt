s3g-dsp @version@ — EXPERIMENTAL Windows x64 CLAP prerelease
==========================================================

121 CLAP files / 128 host-visible plug-in descriptors. Ambi Energy is Mac-only
and is not included. This is an unsigned, native MSVC Release build, not a
stable or certified Windows release. Native Windows ARM64 is not included.
Windows testing has used Windows 10 x64 and REAPER; Windows 11 is not validated.

INSTALL
1. Save your projects, retain your previous plug-ins for rollback, and quit
   REAPER before replacing files. Never overwrite a loaded plug-in.
2. Extract the entire ZIP into a fresh folder. Add that folder to REAPER's CLAP
   search paths and rescan. This package does not install anything itself.
3. Keep all .clap files beside Resources, including its fonts, atlases and
   licenses. Fonts need no system installation. The Mac installer is not used.
4. Remove older pilot/suite folders from the host's scan paths, retaining them
   elsewhere as backups; duplicate CLAP identifiers can load the wrong build.
5. Start with a disposable project and conservative monitoring levels.

Check resizing, preset/project recall, automation, sample import, Unicode paths,
multichannel routing and MIDI in your own setup. Tracker is MIDI-only: insert a
receiving instrument after it. Some processors generate sound without input.
Older CPUs may struggle with high ambisonic orders or point/voice counts;
reduce these or raise the audio buffer if playback drops out. The test laptop
(i7-4510U / 8 GB) is not a recommended minimum system specification.

CONTENTS
Resources includes Fira Code, Tracker's IBM Plex Mono fonts, dependency notices,
and the complete Imprint and Ray atlases (19 responses each). VOT Wavetables
and Ambi Vox Demo Voicebank are optional loadable example assets. Vox includes
WORLD support. Retain LICENSE.txt, TRACKER-LICENSE.txt and THIRD_PARTY_NOTICES.md.

PLUGIN_MANIFEST.tsv records canonical filenames and primary IDs; DESCRIPTORS.json
records every runtime descriptor. EXCLUDED.txt records Ambi Energy's exclusion.
SOURCE_PROVENANCE.txt identifies the source commit and build status.
SHA256SUMS.txt verifies the extracted files; the adjacent .zip.sha256 verifies
the archive. Checksums establish file integrity, not publisher trust.

OPTIONAL TRACKER CHECK
run-windows-check.cmd opens a brief integration check without installing or
starting REAPER. It tests Tracker only, not the full suite or actual host use.
If Windows blocks an unsigned executable, report the warning; do not disable
system security protections. TRACKER-README.txt has additional testing details.

Documentation: https://s3g.github.io/s3g-dsp/
Issues: https://github.com/s3g/s3g-dsp/issues
Include plug-in/preset, OS, REAPER version, CPU, audio driver, sample rate,
buffer size, display scale, and reproducible steps when reporting a problem.
