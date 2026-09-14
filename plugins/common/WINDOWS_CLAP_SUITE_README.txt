s3g-dsp - consolidated Windows x64 CLAP TEST BUILD
================================================

Package: @name@
Inventory: @plugin_count@ canonical CLAP files from scripts/clap-bundles.tsv.
Only Ambi Energy is excluded. Its separate renderer prototype is not a plugin.
Tracker is included with the new VIEW Live Code commands.

STATUS
This is an unsigned development/test package cross-built on macOS, not an
accepted Windows release. All included targets were rebuilt, Windows x64 PE
files and CLAP exports checked, and resources/licenses packaged together.
Successful compilation and packaging do not establish native Windows/REAPER
compatibility. Native host, GUI, file-dialog, MIDI, and DSP testing is required.
Ambi Encoder Vox includes WORLD analysis/resynthesis support.

INSTALL FOR TESTING
1. Save work and quit REAPER before replacing a loaded plugin.
2. Extract the entire ZIP. Use this as a fresh, dedicated CLAP test folder.
   Add that folder to REAPER's CLAP search paths if necessary, then rescan.
3. Keep all .clap files beside the Resources folder. Do not copy just the DLLs,
   flatten Resources, or move its contents into the individual plugins.
4. Avoid duplicate copies from the older family/pilot ZIPs. Remove their test
   directories from REAPER's search paths (or move the old folders outside
   those paths) while retaining them as backups. This ZIP installs nothing
   automatically and does not delete or migrate older files.
5. Fonts are private plugin resources; no system font installation is needed.
   Keep LICENSE.txt, TRACKER-LICENSE.txt, THIRD_PARTY_NOTICES.md, and all font
   and dependency notices in Resources. Resources also includes the complete
   Imprint Atlas and Ray Atlas, each with 19 responses.

CONTENTS AND CHECKS
PLUGIN_MANIFEST.tsv lists every included canonical filename, CLAP identifier,
and host name. EXCLUDED.txt records Ambi Energy's exclusion. SHA256SUMS.txt
covers every payload file except itself. The adjacent ZIP .sha256 file checks
the archive. SOURCE_PROVENANCE.txt records current worktree source hashes;
this is not a source release, notarization, signature, or clean-tag assertion.

OPTIONAL TRACKER CHECK (TRACKER ONLY, NOT A FULL-SUITE TEST)
Double-click run-windows-check.cmd. It invokes the included Tracker integration
test, briefly opens test windows, and reports PASS or FAIL. It does not install
plugins or start REAPER. Read TRACKER-README.txt for the full Tracker checklist.
If Windows blocks an unsigned executable, report the warning; no security
settings need to be disabled. No administrator privileges are required.

REAPER TESTING
Start with a disposable project and conservative monitoring levels; several
plugins generate sound without an input. Check plugin discovery, GUI opening,
65-200% enlargement/resizing, parameter automation, preset recall, and resource
loading. Test sample/asset import and project save/reopen with Unicode paths.
Check multichannel routing against the plugin's documented channel layout.
Tracker generates MIDI only; put a receiving instrument after it. Report the
plugin name, REAPER version, display scale, sample rate/buffer size, and exact
steps if a problem occurs. Keep the old test packages for rollback.
