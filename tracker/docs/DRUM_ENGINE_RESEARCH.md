# Drum engine direction

## Decision

The core sound roadmap targets hard techno, gabber, glitch, jungle, and
breakbeat directly. It does not use the number of emulated chips as a proxy for
genre coverage.

Build these in order:

1. A native stereo slice sampler with sample-accurate onset/retrigger, manual
   and transient slices, start/end/reverse, per-hit pitch, choke groups,
   bounded polyphony, and destructive color modes: bit depth, sample-rate
   reduction, interpolation choice, filtering, and drive. Preserve a future
   route from each voice to stereo, quad, or discrete buses.
2. Dedicated native drum instruments: retain Membrane Kick; add a snare built
   from body resonators, wire/noise, and transient bursts; add tom/body voices;
   add closed/open hats and cymbals from an inharmonic oscillator/noise bank
   with articulation choke.
3. Add a kick-processing device chain with pre/post distortion, asymmetric and
   hard-clip curves, resonant filtering, resampling, DC blocking, and output
   compensation. Gabber character should be designed as synthesis plus a
   reorderable processing chain, not frozen into one kick preset.
4. Use selected, pinned DaisySP drum and effect modules as implementation
   references or adapted DSP cores where listening tests show they close a
   concrete gap. Keep the tracker event, asset, voice, and graph layers native.
5. Put new chip emulators on hold. Retain the existing PSG and YM2151 as
   optional color instruments, but do not deepen them or add OPL/OPN cores
   before the sampler and dedicated drum set are musically convincing.

## Why this order

| Candidate | Direct genre value | Unique contribution | Decision |
| --- | --- | --- | --- |
| Native slice sampler | Essential for breaks, edits, gabber layers, and resampled sound design | User audio, exact slicing, tracker retrigs, future multichannel routing | Build first |
| Dedicated drum DSP | Essential for coherent kick/snare/tom/hat control | Purpose-built synthesis and shared semantic parameters | Build first |
| YMF262 / OPL3 | Strong for synthetic percussion and metallic color | Hardware rhythm mode: bass drum, snare, tom, cymbal, hi-hat | Hold |
| YM2151 / OPM | Useful FM bass, tones, metallic hits, and glitch | Eight four-operator voices; already integrated | Maintain only |
| YM2610 / OPNB | Useful hybrid arcade/FM/sample color | FM + SSG + six ADPCM-A voices + one variable-rate ADPCM-B voice | Later evaluation |
| SN76489-style PSG | Useful noise, ticks, zaps, and rigid square tones | Fast original engine already integrated | Keep; deepen only for a musical need |
| Paula-style playback | Very useful jungle/breakbeat coloration | Period-based 8-bit playback behavior | Implement as native sampler modes before full emulation |

Yamaha's YMF262 data sheet specifies five rhythm sounds, 18 melodic voices or
15 plus five rhythm voices, eight waveforms, and four-channel output. That is a
more specific percussion contribution than another general melodic FM core:
[YMF262 data sheet](https://www.bitsavers.org/components/yamaha/YMF262_199411.pdf).

YMFM already supplies BSD-3 licensed OPM, OPN, and OPL families, including
YMF262 and YM2610, so the existing adapter boundary remains usable:
[YMFM upstream](https://github.com/aaronsgiles/ymfm). The YM2610 document lists
six fixed-rate ADPCM-A channels and one variable-rate ADPCM-B channel, but those
sample-oriented sections also require an explicit asset/provenance design:
[YM2610 data sheet](https://dtech.lv/files_ym/ym2610.pdf).

For native drum prototypes, DaisySP is an MIT-licensed C++ reference with
analog/synthetic bass drum and snare modules plus a hi-hat module. Import only
selected audited modules at a pinned revision, retaining their notices and
adapting them behind the tracker-owned node/event contract:
[DaisySP](https://github.com/electro-smith/DaisySP) and its
[drum sources](https://github.com/electro-smith/DaisySP/tree/master/Source/Drums).

For later high-quality offline or latency-declared warping, Signalsmith Stretch
is an MIT C++ candidate. Its own documentation says time stretching is best for
moderate 0.75x–1.5x changes, so extreme jungle character still needs native
repitch/resample modes and deliberate destructive algorithms:
[Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch).

## Immediate implementation slice

- Five selected DaisySP models are now implemented as indexed native
  instruments with exact-offset triggering, private state, presets, audition,
  and model-specific editors. Their vendored revision and local realtime-safe
  random-state patch are recorded under `third_party/daisysp/`.
- Integrate the delivered immutable-asset, 128-slice, 32-voice stereo sampler
  core into the indexed rack and add background file loading.
- Evaluate the DaisySP snare/hat voices against a small reference render corpus
  and retain them where convincing; build purpose-specific replacements where
  the custom membrane approach is stronger.
- Add sound-quality gates based on rendered fixtures, not only nonzero-energy
  tests: transient length, spectral centroid/noise balance, pitch sweep,
  deterministic reset, sample-offset onset, and finite-output checks.
