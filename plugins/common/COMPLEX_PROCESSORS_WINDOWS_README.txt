s3g-dsp: No Input Mixer, Formant Matrix, Ambi Encoder Vox, Ambi Imprint
Windows x64 CLAP / VSTGUI test batch

Copy the entire extracted folder to a CLAP location scanned by REAPER, such
as %LOCALAPPDATA%\Programs\Common\CLAP\s3g-dsp. Keep Resources beside the four
.clap files. Remove or move older copies of these same four plugin IDs outside
all scanned CLAP folders to avoid duplicate versions. Restart REAPER/rescan.

The GUI uses bundled Fira Code and the shared 65-200% proportional resizing.
Resources/Fonts includes the font and OFL license. Resources/Licenses includes
RapidJSON and WORLD notices. No separately installed font, WORLD DLL or MinGW
runtime is needed. Resources/Imprint Atlas contains all 19 factory responses.

No Input Mixer: eight-channel self-generating feedback instrument. Keep output
low while testing. Check PATCH/MIXER/CHANNEL/SAFETY/AUX, cable/grid editing,
pop-out/dock, lane/aux effect editors, presets, MIDI control and PANIC.

Formant Matrix: route live input and/or MIDI/internal speech as in the Mac
version. Check ROUTE/BANK/SOURCES/PHRASE/FX, the 22/16-band A/B matrix, signed
crosspoints, trims, phrase compilation, factory/user presets and meters.

Ambi Encoder Vox: 64-channel ACN/SN3D vocal instrument. Follow it with an
ambisonic decoder. The PHRASE LOAD action offers VOCAL WAV FILE or VOICEBANK
FOLDER, then opens the corresponding file/folder picker. Voicebanks use oto.ini.
WORLD analysis is included; first import can take time. Analysis cache lives in
%LOCALAPPDATA%\org.s3g.s3g-dsp\AmbiVoxWorld. Check multiline lyrics/cue selection,
generator, FREE/MIDI/BOTH, SPEAK/TEXTURE, field cursors and spectral controls.
.s3gvox presets store performance settings, not external vocal audio. Loading
one preserves the currently loaded source. Project-state source storage has
not been redesigned in this migration: keep your original WAVs/voicebanks.

Ambi Imprint: 64-channel ambisonic effect, not a source instrument. Match the
order to your track and load an ATLAS response or .s3gimprint. Check room/field
geometry, camera rotate/zoom, MIX/FOCUS/WIDTH, field listening and preset recall.

This batch was cross-compiled on macOS, not runtime-tested in Windows REAPER.
Please check file/folder dialogs (including non-ASCII paths), font readability,
65/100/150/200% sizing, automation/undo and save/reopen on the Windows machine.
Tracker and Ambi Energy remain Mac-only and are intentionally excluded.
