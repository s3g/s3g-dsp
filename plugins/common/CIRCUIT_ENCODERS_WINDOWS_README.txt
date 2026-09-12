s3g-dsp — Pulsar / Neural Ecology / Wrangler VSTGUI Windows x64 test build

Copy this entire folder to a CLAP search location used by REAPER, or add the
folder to REAPER's CLAP path, then rescan. Keep Resources beside the .clap files.
Do not retain another copy of the same plugin in a second scanned folder.

Included instruments (64 outputs, no track-audio input):
  s3g Ambi Encoder Pulsar 64
  s3g Ambi Encoder Neural Ecology 64
  s3g Ambi Encoder Wrangler 64

Fira Code is private to these plugins; users do not need to install the font.
Its redistribution license is Resources/Fonts/FiraCode-LICENSE.txt.
A missing font resource falls back safely, but may change text appearance.

Editors retain their Cocoa geometry and controls, use custom separated menus,
and resize proportionally from 65% to 200%. Drag the host editor's window edge
to resize. Neural Ecology accepts MIDI for lattice navigation, not pitched
excitation. Pulsar and Wrangler generate audio without MIDI.

LOAD/SAVE use each instrument's original preset extension:
  Pulsar:         .s3gpulsar
  Neural Ecology: .s3gne
  Wrangler:       .s3gawp
Preset recall preserves OUT. Full plugin/project state restores OUT as well.
Pulsar's captured tables remain runtime material under its existing behavior.
Neural Ecology preserves its resident genomes and evolutionary lattice.
Wrangler's single-preset files contain one instrument state; its complete
multi-cell SURF map is stored in the host project.

Windows REAPER acceptance checks:
  - First-open points, all view/page switches, separated menus and 65–200% size.
  - Presets with non-ASCII filenames; save/reload a REAPER project.
  - Pulsar: all three lanes, wave previews, CAPTURE and LISTEN/NEURAL displays.
  - Neural Ecology: GROW, 1/2/4/8 planes, STOP/cell audition, GO/PLAY and MIDI.
  - Wrangler: CURVE labels only select; dragging locks one dimension/voice.
    Test WRITE/SETTLE, SURF cells, X/Y automation, POP menus and synchronization.
  - Record a slider gesture and play it back; close/reopen the editor.

The x64 binaries were cross-built on macOS and checked for CLAP exports and
external MinGW runtime dependencies. Mac native GUI tests, CLAP validation and
Cocoa/portable audio-state comparisons are automated. Windows REAPER itself
still needs acceptance testing on the Windows machine.
