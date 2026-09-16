# Consolidated Windows CLAP preview review

This branch collects the accepted Windows Tracker, font, native-build, drawing
and DSP changes used by installed performance preview **20260915.4**. It is the
single branch to review and publish for this Windows preview. Earlier issue and
diagnostic worktrees remain preserved; do not merge them again into this branch.
Rejected experiments and profiling instrumentation are excluded.

## Repository and history

- Repository: `https://github.com/s3g/s3g-dsp.git`.
- Working checkout: `C:/src/s3g-dsp`.
- Branch: `windows/windows-preview`.
- Intended publish destination: `origin/windows/windows-preview`.
- Current `origin/main`: `6c656681ff303ce26cf133991a4eb0ef4870cbac`.
- Retained existing Tracker HEAD: `6320a047b6d2341f7000af7c32b2471e90e51ef0`.
- Existing commits `3de457c` and `6320a04` are preserved as ancestors. No rebase,
  squash, merge commit, new commit or push was performed during consolidation.
- The tested source contains 71 changed/new paths relative to main. Five already
  match the existing Tracker commits; 66 source/test/build paths plus this review
  document remain for the user's commit (67 paths total).

The user explicitly requested this consolidated preview branch. It is intended
for review and eventual integration into shared main, not a permanent Windows
fork. The previous one-issue branches remain historical evidence.

## Included behavior

- Tracker Windows page activation, keyboard/dialog-key ownership, clipboard,
  large factory-state stack avoidance and Assemble recall preservation.
- Bundled font selection, Windows text-layout caching and native suite build
  compatibility, including the Windows spectral FFT backend.
- Scoped Windows floating-point/denormal handling for measured DSP workloads,
  sample cursor optimization, accepted Pyrosphere coefficient and lower-order
  encoder work.
- Sample Rings immutable-asset validation and waveform geometry caching,
  Formant filter/consonant work, Spectral FFT butterflies and mask caching.
- Necessary regression tests and Windows-only build/test wiring.

CLAP IDs, parameter IDs, saved formats, host-timed MIDI, routing, bundled fonts,
menus, focus behavior and accepted GUI features are preserved. Tracker remains
CLAP-only; Ambi Energy is excluded. Production changes preserve non-Windows
paths through the existing Windows guards and prior source audits. This does
not substitute for Mac compilation/runtime acceptance.

## Validation and remaining limits

The consolidated source matches the exported, tested preview source exactly;
only this review document is additional. Consolidation checks include a complete
tracked-tree comparison, all 71 accepted file hashes, whitespace checks and
reapplication of both exported patches to temporary indexes. The real staging
index is left for the user. No fresh build/audio/GUI test is claimed for moving
these identical source files into the main checkout.

Prior native MSVC x64 builds and Windows 10/REAPER 7.79 evidence cover all 121
CLAPs within the per-plugin matrix's fresh/inherited scopes. Tracker keyboard,
pages, resizing, recall and host-MIDI checks, bundled font checks, and suite GUI
lifecycle checks are recorded. The final six optimized binaries passed native
comparisons covering 199,269,376 bit-identical output samples, plus FFT/Formant
regressions and actual REAPER editor and performance observations.

After installation, all 226 installed files verified; all 121 CLAPs and 69
resources match the approved package. The installed font checker selected
bundled Fira Code, Tracker passed 1,059 native integration checks, and actual
REAPER checks passed opening, hide/show, recall and two simultaneous instances
for all six updated editors. Those final GUI checks had audio closed.

The tested machine is Windows 10 Education 22H2 19045.6456, i7-4510U, 8 GB,
Intel HD 4400, REAPER 7.79 x64, Realtek WASAPI at 48 kHz/512. Heavy Ambi, Formant
and Spectral workloads can still exceed its real-time budget; Rings retains
drawing overhead. No universal RAM minimum, sustained dropout-free listening,
Windows 11, or new Mac/CI acceptance is claimed. The earlier strict-state sweep
retains its ten documented exceptions, including Formant Audition and Rings
Capture Gate reset behavior. CI, Mac-impact review and applicable Windows/Mac
acceptance remain prerequisites for merging into main.

## Preserved evidence

Local evidence root: `C:/s3g/s3g-dsp-windows-handoff/windows-testing/`.

- `integration/PRIORITY_RESULTS_20260915.md`: final measurements and limitations.
- `integration/PRIORITY_PER_PLUGIN_RESULTS_20260915.csv`: exact per-plugin hashes
  and coverage; earlier suite/family/DSP reports are retained alongside it.
- `integration/PRIORITY_PREVIEW_INSTALLATION_20260915.md`: installation, backup,
  exact installed paths and post-install checks.
- `integration/consolidated-windows-preview-20260915/`: original pending-file
  backup, original index, before/after status, exact commit list, hash comparison,
  review receipt and two alternative patches. The patch against Tracker HEAD
  and the full patch against main are alternatives, not cumulative layers.
- `integration/priority-review-20260915/`: original 71-path tested-source patch
  and prior issue reviews, all preserved unchanged.

Approved tester ZIP SHA-256:
`61afeb0d02660dd08c29c4ff54cb982e0de9c5cd5f02643923ccac049381993b`.

Original tested 71-path source patch SHA-256:
`ea4d2bdd9e9e883331c83d6730e1b97aaaa29033f412e00844fd07884a0d0fcf`.

Builds, dependencies, binaries, packages, backups and raw logs stay outside the
commit. The frozen tester package is not rewritten by this consolidation.

## GitHub Desktop handoff

1. Open the existing repository at `C:/src/s3g-dsp` and verify the current branch
   is **windows/windows-preview**.
2. Review the exact 67 paths below. Suggested commit summary:
   **Complete Windows CLAP preview fixes and performance improvements**.
3. Commit the reviewed files, then publish this branch to
   **origin/windows/windows-preview**. Publishing before committing will not
   include these pending changes. No other Windows branch needs to be published
   to include the accepted preview source.
4. Use a reviewed PR into `main`; complete applicable CI and Mac acceptance
   before merging. Retain the earlier branches until that integration is done.

Exact pending commit paths:

```text
cmake/S3GVSTGUIWindowsClipboard.cmake
cmake/S3GVSTGUIWindowsTextLayout.cmake
cmake/windows/s3g_windows_text_layout_cache.h
cmake/windows/vstgui_clipboard_text.inc
CMakeLists.txt
dsp/s3g_acapella_resonator_bank.h
dsp/s3g_ambi_insect_encoder.h
dsp/s3g_ambi_pyrosphere_encoder.h
dsp/s3g_ambi_water_encoder.h
dsp/s3g_ambi_wind_encoder.h
dsp/s3g_pyrosphere_windows_coefficients.h
dsp/s3g_spectral_fft.h
dsp/s3g_spectral_mesh.h
dsp/s3g_spectral_windows_fft.h
dsp/s3g_windows_encoder_basis.h
plugins/clap_8ch_spectral_spray/s3g_8ch_spectral_spray_clap.cpp
plugins/clap_acapella_source_synth/s3g_acapella_source_synth_clap.cpp
plugins/clap_ambi_imprint/s3g_ambi_imprint_clap.cpp
plugins/clap_ambi_object_decoder/s3g_ambi_object_decoder_clap.cpp
plugins/clap_ambi_pyrosphere_encoder/CMakeLists.txt
plugins/clap_ambi_pyrosphere_encoder/s3g_ambi_pyrosphere_encoder_clap.cpp
plugins/clap_ambi_pyrosphere_encoder/s3g_pyrosphere_windows_float_mode.h
plugins/clap_array_calibrate/s3g_array_calibrate_clap.cpp
plugins/clap_delay_processor/s3g_delay_processor_clap.cpp
plugins/clap_drum_mixer/s3g_drum_mixer_clap.cpp
plugins/clap_macro_fracture/s3g_macro_fracture_clap.cpp
plugins/clap_macro_shred/s3g_macro_shred_clap.cpp
plugins/clap_no_input_mixer/s3g_nim_windows_bit_scan.h
plugins/clap_no_input_mixer/s3g_no_input_mixer_clap.cpp
plugins/clap_processor_fissure/s3g_processor_fissure_clap.cpp
plugins/clap_processor_lowform/s3g_processor_lowform_clap.cpp
plugins/clap_relay/s3g_relay_clap.cpp
plugins/clap_sample_doubles/s3g_sample_doubles_clap.cpp
plugins/clap_sample_lanes/s3g_sample_lanes_prelude.inc
plugins/clap_sample_motion/s3g_sample_motion_clap.cpp
plugins/clap_sample_rings/s3g_sample_rings_clap.cpp
plugins/clap_sample_wavesets/s3g_sample_wavesets_clap.cpp
plugins/clap_spectral_spray/s3g_spectral_spray_clap.cpp
plugins/clap_spectral_topology_processor/s3g_spectral_topology_processor_clap.cpp
plugins/clap_tracker/Windows.cmake
plugins/common/s3g_sample_family_vstgui.cpp
plugins/common/s3g_sample_family_vstgui.h
plugins/common/s3g_vstgui_foundation.cpp
plugins/common/s3g_windows_dsp_float_mode.h
plugins/common/s3g_windows_processor_float_mode.h
plugins/common/s3g_windows_sample_rings_cache.h
plugins/common/s3g_windows_string_compat.h
tests/clap_state_fixture_smoke.cpp
tests/nim_windows_bit_scan.cpp
tests/pyrosphere_windows_float_mode.cpp
tests/spectral_windows_fft.cpp
tests/tracker_windows_clipboard.cpp
tests/vstgui_dropdown_smoke.cpp
tests/vstgui_windows_font_smoke.cpp
tests/windows_dsp_float_mode.cpp
tests/windows_encoder_basis.cpp
tests/windows_processor_float_mode.cpp
tests/windows_pyro_coefficients.cpp
tests/windows_pyro_coefficients_adapter.cpp
tests/windows_pyro_coefficients_api.h
tests/windows_spectral_butterflies.cpp
tests/windows_text_layout_cache.cpp
tracker/include/s3g/tracker/editor_authoring.h
tracker/src/editor/editor_assemble.cpp
tracker/src/editor/s3g_tracker_assemble_page.cpp
tracker/tests/editor_authoring_tests.cpp
WINDOWS_PREVIEW_REVIEW.md
```
