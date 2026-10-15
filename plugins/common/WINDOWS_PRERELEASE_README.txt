s3g-dsp @version@ — EXPERIMENTAL Windows x64 CLAP prerelease
==========================================================

125 CLAP files / 132 host-visible plug-in descriptors. Ambi Energy is Mac-only
and is not included. This is an unsigned, native MSVC Release build, not a
stable or certified Windows release. Native Windows ARM64 is not included.
Windows testing has used Windows 10 x64 and REAPER; Windows 11 is not validated.

INSTALL
1. Save your projects, retain your previous plug-ins for rollback, and quit
   REAPER, Max and Live if open. Never overwrite a loaded plug-in.
2. Extract the entire ZIP to a fresh folder. Move older s3g-dsp copies outside
   all host CLAP scan paths, retaining them as backups.
3. Copy the extracted folder's complete contents to a fresh
   %COMMONPROGRAMFILES%\CLAP\s3g-dsp folder (usually
   C:\Program Files\Common Files\CLAP\s3g-dsp). Windows may ask for
   administrator permission. Keep all .clap files beside Resources, including
   fonts, atlases and licenses. This package does not install itself.
4. In REAPER Preferences > Plug-ins > LV2/CLAP, confirm
   %COMMONPROGRAMFILES%\CLAP is listed, then rescan. This location also works
   with s3g.clap~ in Max for Live.
5. Start with a disposable project and conservative monitoring levels. Fonts
   need no system installation; the Mac installer is not used.

If you cannot use Common Files, install under
%LOCALAPPDATA%\Programs\Common\CLAP\s3g-dsp instead and add
%LOCALAPPDATA%\Programs\Common\CLAP to REAPER's CLAP path list. Do not keep a
second copy in %APPDATA%\CLAP or another scanned folder: duplicate CLAP IDs
can load the wrong build.

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
