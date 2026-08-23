# s3g-dsp 0.8.0-pre

Apple silicon macOS CLAP collection and optional No Input Mixer standalone pre-release, prepared August 21, 2026.

## Assets

- `s3g-dsp-macos-clap-0.8.0-pre.zip` — 128 CLAP bundles containing 132 plug-ins for REAPER
- `s3g-no-input-mixer-app-macos-arm64-0.8.0-pre.zip` — standalone No Input Mixer application

Both archives contain arm64 binaries for Apple silicon Macs (M1, M2, M3, M4, or newer). They are not compiled for Intel Macs and are not universal binaries. The standalone app currently requires macOS 15 or newer and is distributed separately from the CLAP collection.

## Highlights

- Adds **s3g Sample Motion 2** and **s3g Sample Motion 32**. Eight trajectories cover Hover, polarity-reflected Mirror, seeded Drunk, alternating Zigzag, one-way Forward/Reverse, a stepping Moving Loop, and non-inverting BaktoBak reading. Normal `1x` follows ordinary source speed, with absolute Hertz available. Continuous, Packets, and nested Motor articulation share four envelope shapes. An allocation-free four-lane event layer adds Freeze, naturalized Iterate, short random-position Pulser packets, contiguous grouped Doublets, accelerating/decaying/shrinking Bounce, and MCH Iterate with Clock/Packet/Turn triggering and Cut/Layer playback. Motion 32 can allocate at note, turn, or segment boundaries, including MCHZIG-style per-turn routing, MCHITER event routing, and optional adjacent-destination avoidance. The Sample-family editor adds asynchronous load/drop/clear, waveform S/E/L editing, zoom/pan/fit, presets, a numbered Source Motion → Sound → Output hierarchy, one exclusive Sound selector, process-specific source/motion relationship readouts, trajectory scope, routed compositor flags, and host-correct automation. Version 0.4.3 adds path-coupling classifications, trigger-dependent control guards, and direct Packet Rate access for Event sounds; 0.4.2 introduced the explicit hierarchy and source-role readouts; 0.4.1 corrected the Motion parameter range so Moving Loop and BaktoBak are selectable from both the editor and host parameter surfaces.
- Adds **s3g Sample Player 2** and **s3g Sample Player 16**, with chromatic polyphonic playback, Rate and duration-preserving Stretch pitch modes, forward/reverse loop and ping-pong modes, host-tempo sync, filter and amplitude envelopes, direct waveform editing and zoom, active-note cursor flags, shared Project/Link/Embed source storage, and stereo or relationship-preserving 16-channel output.
- Adds **s3g Sample Doubles 2**, a stereo two-read-head instrument with independent deck transport and levels, linked control, velocity-sensitive drag, zero-crossing cues with audible pre-roll and retriggering, analyzed or manual sample tempo, beat-based offsets and phase steps, live Deck B phase and drift, three crossfader curves, presets, and direct Tracker/MIDI command and CC mappings.
- Adds **s3g Sample Wavesets 2** and **s3g Sample Wavesets 32**. Both turn analyzed crossing-cycle groups into chromatic mono, legato, or polyphonic voices with source and group scopes, cycle ordering, repetition and stride, and a set of real-time waveset transformations. Wavesets 32 adds a reusable trigger-time allocator for 2–32 active outputs, mono destinations or stereo pairs, and sequential, reverse, palindrome, random, and no-repeat random routing.
- Revises **s3g Sample Slicer 16** around the shared Sample-family file, waveform, project-media, MIDI-start-note, editor, mixer, mutation, and documentation conventions while preserving its fixed 16-channel routing.
- Extends **s3g Tracker** with MIDI step recording, held-note editing, undo and project history, runtime pattern planning, clearer note activity, expanded Song and timing behavior, and a scripted REAPER acceptance workflow. These additions support the new Sample-family note, CC, retrigger, and routing workflows directly.
- Expands **Processor Stack** with Stack Score, host synchronization, and additional controlled random-thinning behavior, and adds updated MIDI handling to Ambi Encoder Membrane Kick.
- Adds shared eight-channel ring-output projection to Processor Fault, Processor Loop, Processor Multi Loop, and Processor No Input Mixer, with direct, quad-ring, and stereo-ring formats plus rotation. Existing saved No Input Mixer states migrate to the unchanged direct-output default.

## Reliability and release checks

- The canonical manifest freezes 128 bundles, including 126 non-NIM bundles, and packaging verifies all 132 runtime plug-in descriptors rather than inferring descriptor count from bundle count.
- The Release build registers 132 CTest cases and the standalone build registers 49 cases. The serial product/test gate covers 128 cases; 128 compiled non-NIM cases are registered for ASan/UBSan, with the package-verifier unit bringing the current non-NIM registration to 129.
- All 126 non-NIM bundle identities pass isolated CLAP validation. Their callback-allocation audits cover the complete manifest with zero realtime allocation operations; all nine strict realtime profiles pass and the tenth 96 kHz environmental profile completes as an allocation-free advisory measurement.
- Dedicated DSP, CLAP, state, GUI, output-allocation, and multichannel routing coverage accompanies the four new Sample-family bundles and their seven plug-in descriptors.
- Sample Doubles, Sample Wavesets, and Sample Motion use persistent compositor-driven cursor trajectories on macOS so predictable playheads remain smooth when a host temporarily delays AppKit presentation after a parameter gesture. Routine meters and static feedback remain on the ordinary GUI refresh path.
- Sample Player, Sample Doubles, Sample Wavesets, Sample Motion, and Sample Slicer now default new instances to Project storage: sources receive content-hashed names and are copied asynchronously into the exact containing REAPER project's media directory, registered for copy-media Save As operations, and referenced with small state. Link retains the original location; Embed remains explicit and reports the decoded PCM state cost. Earlier Embed and path-only sessions migrate without changing their storage behavior.
- The shared voice-output allocator and ring-output mixdown are covered as independent DSP components as well as through the plug-ins that consume them. Callback-allocation, CLAP validation, sanitizer, and realtime gates continue to derive their product inventory from the release manifest.
- Documentation adds complete Sample Player, Sample Doubles, Sample Slicer, Sample Wavesets, and Sample Motion guides and current native captures, reorganizes the Sample-family and CRCLTR navigation, and updates the Tracker, Processor Stack, multichannel-output, and No Input Mixer material. The static audit checks 126 HTML pages and 4,876 local references.

---

# s3g-dsp 0.7.0-pre

Apple silicon macOS CLAP collection and optional No Input Mixer standalone pre-release, released August 14, 2026.

## Assets

- `s3g-dsp-macos-clap-0.7.0-pre.zip` — 124 CLAP bundles containing 125 plug-ins for REAPER
- `s3g-no-input-mixer-app-macos-arm64-0.7.0-pre.zip` — standalone No Input Mixer application

Both archives contain arm64 binaries for Apple silicon Macs (M1, M2, M3, M4, or newer). They are not compiled for Intel Macs and are not universal binaries. The standalone app currently requires macOS 15 or newer and is distributed separately from the CLAP collection.

## Highlights

- Expands the release inventory from 98 to 124 CLAP bundles. The Slicer bundle contains both its stereo and 16-channel plug-ins, bringing the collection to 125 plug-in descriptors. The complete procedural drum family now includes Kick, Snare, Floor Tom, Concert Bass, Toms, Hi-Hat, Clap, Cowbell, Crash, Break, Drum Overload, Drum Echo, and Drum Mixer 16.
- Adds **s3g Tracker**, a host-synchronized polymetric MIDI tracker with 32 lanes, pattern and Song workflows, timing warps, Live Code, and eight REAPER-routable MIDI output buses.
- Adds both **s3g Slicer** variants, sharing a four-break sample engine, embedded project media, per-break MIDI mapping, simultaneous waveform overviews, and stereo or fixed 16-channel output.
- Adds Ambi Encoder Membrane Kick 16, Ambi Encoder Acid 16, and Ambi Encoder Horizon, and brings the expanded landscape, site-processing, and listener system for Ambi Encoder Cartography into the archive.
- Adds Processor Errant and Processor Feedback Shift, including Errant's repeatable glitch genealogies and Feedback Shift's paired eight-node scenes, virtual patch splicing, per-node inserts, in-network aux processing, and post-network granulation.
- Adds **Processor Formant Matrix**, a voice-responsive synthesis instrument and resonant filter-bank effect with a signed 22-by-22 routing matrix, live or procedural speech articulation, MIDI or voice-tracked carriers, scenes, and post-bank processing.
- Adds **Processor Fissure**, an optional-input eight-cell physical noise system with contact and shaker excitation, a paintable signed matrix, scenes, performed fracture gestures, and Stereo, Quad, or 8 Direct output on one stable eight-channel port. Named values and physical units round-trip in host text entry, and project state preserves the complete fracture control surface while retaining compatibility with earlier saved formats.
- Adds **Processor LF Synth**, **Processor Stack**, and **Processor Conduit**. They cover sustained modal bass, dual-player guitar rigs with independent arpeggiators and governed cross-feedback, and stereo-default live vocals sent through virtual materials, piezo pickups, movable pedals, octave-down drag, and a moving-PA feedback path.
- Extends Macro Shred's multichannel interface with an eight-second feedback energy and governor-reduction history while preserving its established controls and bounded loop behavior.
- Adds dedicated NIM Gesture key-feedback routing to the standalone No Input Mixer application alongside the existing E16 and BU16 feedback paths.

## Reliability and release checks

- New products participate in the normal CLAP, Release, sanitizer, manifest, validator, allocation, and realtime configurations through the standard build presets.
- The Release suite passes 112 CTest cases, including focused DSP, CLAP contract, GUI, state, realtime, and Tracker workspace tests. Dedicated drum-family, Membrane Kick, Acid, and Horizon audits cover their full product boundaries.
- The canonical manifest requires exactly 124 bundles, including 122 non-NIM bundles. Packaging and extracted-archive verification use the same inventory and reject partial, duplicate, or mismatched bundles.
- All 122 non-NIM bundles pass isolated CLAP validation and the callback-allocation sweep. The non-NIM ASan/UBSan suite passes 109 tests, including the new processors' DSP, CLAP, GUI, state, and routing behavior.
- Documentation covers 121 checked HTML pages and 4,708 local references. New native captures document Processor LF Synth, Processor Stack, Processor Conduit, Processor Errant, the s3g Slicer editor and mixer, and Drum Echo. Refreshed meter-bearing captures retain active signal or network history, and new top-down SVG diagrams document the Formant Matrix and Stack signal paths.
- Ten realtime profiles cover core, wide-buffer, Spectral Topology, Spectral Spray, Water, and Insect processing with allocation evidence. All strict profiles pass; the documented 96 kHz maximum-density Water/Insect profile also remains stable and allocation-free as an advisory measurement.

---

# s3g-dsp 0.6.0-pre

Apple silicon macOS CLAP collection and optional No Input Mixer standalone pre-release, released August 2, 2026.

## Assets

- `s3g-dsp-macos-clap-0.6.0-pre.zip` — 98 CLAP products for REAPER
- `s3g-no-input-mixer-app-macos-arm64-0.6.0-pre.zip` — standalone No Input Mixer application

Both archives contain arm64 binaries for Apple silicon Macs (M1, M2, M3, M4, or newer). They are not compiled for Intel Macs and are not universal binaries. The standalone app currently requires macOS 15 or newer and is distributed separately rather than being embedded in the CLAP collection archive.

## Highlights

- Expands the collection to 98 CLAP products, with a canonical product list, an identity-verified installer, and automatic backups of recognized renamed or retired aliases.
- Adds **s3g Processor No Input Mixer 8ch**, an output-only feedback instrument with an interactive signed routing matrix, signal-aware wiring, movement and articulation behaviors, listener-reactive control, three inserts per lane, two feedback-capable AUX processors, per-lane EQ and tuning, energy-scaled randomization, presets, MIDI control, and detachable page windows. The optional NIM Gesture utility supports recording controller gestures.
- Adds Ambi Encoder Medium 16 and the mono, 8-channel, and 24-channel Macro Fracture family. Ambisonic encoders and instruments now cover point, cloud, terrain, path, ray, bilocation, the Modal encoder, VOT wavetable, voicebank and WORLD resynthesis, wave terrain, wind, water, pyrosphere, cryosphere, insect, pulsar, neural ecology, stochastic, and wrangler workflows.
- Adds ambisonic DJ filter, delay, pitch, gain, Resonance Print, Partial Trace, Response Trace, Displacement, and Imprint effects, together with expanded speaker, adaptive, object, stereo, head, and utility decoding.
- Adds Processor Fault, CRCLTR, Macro Shred, shared listener mode, Parameter Surface integration, multichannel monitoring, and expanded matrix, panner, calibration, and distributed-output tools.
- Includes 28 VOT wavetable banks and the synthetic Ambi Vox demonstration voicebank, with third-party attribution and loading instructions. Documentation now includes 91 checked HTML pages with native GUI captures, responsive-layout checks, signal-flow explanations, parameter references, and installation guidance.

## Reliability and release checks

- Makes live controls safer in the environmental, terrain, and direct-panner families by handing GUI changes to the audio engine instead of directly changing active processing. It also fixes an Ambi Vox editor memory error found by memory-safety testing. Closing the editor now fully disconnects its window before any delayed actions can reach it.
- Improves saved-session reliability across the stateful plug-ins, including hosts that save or restore data in small pieces. The 8- and 24-channel Spectral Topology versions now save reproducibly while remaining compatible with earlier sessions. Speaker Decoder waits for its background layout update before saving, and Stochastic Encoder saved settings and displayed parameter values are corrected.
- Restores Layout, DBAP, LBAP, and VBAP Panner sessions with the saved layout, selected source, and coordinates together, avoiding inconsistent REAPER channel behavior after using several panner instances. Neural Ecology now preserves existing session seeds across its full 32-bit Circuit Seed range, and named values and units are corrected across encoder, decoder, processor, matrix, mixer, and compact-effect families.
- Removes extremely small output residues from Bilocation, Layout Panner, and Orbit Delay. Realtime tests now reject invalid or near-zero “subnormal” output samples. Presets preserve the user's output-volume setting, as Random already does, and AED controls, processing, and diagrams consistently place +90 degrees on the left and -90 degrees on the right.
- Automated testing now validates the complete CLAP collection in isolation and includes 53 CTest cases, memory and undefined-behavior checks (ASan/UBSan), randomized rotation comparisons, environmental filter A/B comparisons, fixed-seed fingerprints, non-default earlier-state fixtures with expected parameter snapshots, and dedicated Wrangler, Fault, and Ambi Vox probes. Regression tests also cover reproducible and buffered state saving, parameter conversions, preset output preservation, and Spectral Spray placed after a wider-channel plug-in on a REAPER track.
- Realtime tests cover the core, wide-buffer, Spectral Topology, Spectral Spray, Water, and Insect weak points without requesting or releasing memory during audio processing. Each supported profile ran at least 10,000 measured blocks per scenario; 99 percent completed within 75 percent of the buffer deadline, with no more than one percent missing the deadline. Raw maxima and miss counts remain available in the JSON evidence. The separate maximum-density Water/Insect test at 96 kHz remained stable and allocation-free but did not meet this timing limit, so it remains an advisory measurement rather than a supported maximum load.
- A separate memory-allocation test covers the exact 96-product non-NIM inventory. All 96 products pass CLAP validation, and all 380 tested scenarios complete with zero memory allocation or deallocation during audio processing. Allocator self-tests and a deliberately allocating test plug-in ensure that this check cannot report a false all-zero result.
- Packaging starts from a fresh Release build. Final-release mode rejects dirty, stale, or non-Release sources, updates package metadata from the running plug-in descriptions before signing, verifies all 98 identities after extracting the ZIP, tests the installer in isolation, and writes a SHA-256 checksum.

## Installation

The packaged pre-release supports REAPER on Apple silicon (`arm64`) Macs. It is not notarized, so macOS requires one Gatekeeper approval for the installer. The installer writes only to the current user's Library and does not use `sudo`.

1. Quit REAPER and unzip the archive.
2. Double-click `Install s3g-dsp CLAPs.command`.
3. If macOS blocks it, click **OK**, open **System Settings > Privacy & Security**, and choose **Open Anyway**. Confirm **Open** and authenticate if asked. This option is available for about one hour after the blocked launch.
4. If prompted, allow Terminal to access the downloaded package, then wait for installation to finish.
5. Start REAPER and rescan CLAP plugins if they do not appear automatically.

The installer places the collection in `~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/`. On upgrades, it moves identity-verified older s3g-dsp bundles—including copies in the former top-level CLAP location—to a timestamped backup. Other CLAP products are untouched.

After approval, the installer verifies each staged bundle, removes only `com.apple.quarantine`, and preserves other extended attributes. Approval applies to the installer, so REAPER should not present a separate Gatekeeper dialog for each plugin afterward.

Pass `--dry-run` from Terminal to preview the exact changes.

## Pre-release notice

This remains pre-release software. Plugin names, parameter mappings, saved-state compatibility, and the included product set may change before a stable release. Stable CLAP identifiers are preserved across the current filename migration.
