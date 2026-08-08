# s3g Break Bus: AUX processor research and design direction

## Decision

Replace the Slicer's temporary `DrumOverload` AUX return with a new processor,
provisionally named **s3g Break Bus**. It should be designed for parallel
breakbeat treatment rather than inherited from the drum-synthesis mixer.

The effect must keep compression, saturation, distortion, and clipping as
separate stages. A single Drive macro cannot distinguish gentle glue from
program-dependent smash, harmonic density, or peak flattening.

## Research findings

Giannoulis, Massberg, and Reiss separate compressor topology, static curve,
level detection, and ballistics, and show that peak/RMS choice, feed-forward
versus feedback structure, knee, attack, and release materially change the
result. Their later parameter-automation work uses real-time sidechain features
and the peak/RMS relationship to adapt compressor timing. Moffat and Sandler
apply adaptive ballistics specifically to percussive material to retain or
emphasize transients.

Nonlinear color requires explicit anti-aliasing. Bilbao, Esqueda, Parker, and
Välimäki demonstrate antiderivative anti-aliasing (ADAA) for hard clipping and
`tanh` saturation, while conventional oversampling remains the general
alternative. This matters especially for gabber-style clipping and pitched-up
breaks, where aliased components are otherwise prominent.

Ableton Drum Buss is a useful boundary reference, not a circuit to reproduce:
it separates compressor, three distortion strengths, mid/high crunch and
damping, transient control, and resonant bass enhancement. Break Bus should
instead prioritize dynamics, transient continuity, and staged nonlinear color;
it does not need another synthetic low-frequency resonator.

Primary references:

- Dimitrios Giannoulis, Michael Massberg, and Joshua D. Reiss,
  [Digital Dynamic Range Compressor Design—A Tutorial and Analysis](https://aes2.org/publications/elibrary-page/?id=16354),
  JAES 60(6), 2012.
- Dimitrios Giannoulis, Michael Massberg, and Joshua D. Reiss,
  [Parameter Automation in a Dynamic Range Compressor](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf),
  JAES 61(10), 2013.
- Dave Moffat and Mark Sandler,
  [Adaptive Ballistics Control of Dynamic Range Compression for Percussive Tracks](https://secure.aes.org/forum/pubs/ebriefs/?elib=19748),
  AES 145 eBrief 484, 2018.
- Stefan Bilbao, Fabián Esqueda, Julian D. Parker, and Vesa Välimäki,
  [Antiderivative Antialiasing for Memoryless Nonlinearities](https://www.pure.ed.ac.uk/ws/portalfiles/portal/34115216/bilbao_pdf.pdf),
  IEEE Signal Processing Letters 24(7), 2017.
- [Ableton Live 12 Audio Effect Reference: Drum Buss](https://www.ableton.com/en/manual/live-audio-effect-reference/),
  official product documentation.

## Proposed signal path

```text
post-fader break sends
  -> detector high-pass and link matrix
  -> feed-forward soft-knee compressor
  -> transient recovery / emphasis
  -> smooth saturator
  -> harder asymmetric distortion
  -> antialiased safety clipper
  -> broad tilt/damping filter
  -> return gain
  -> dry break mix
```

The compressor and transient detector observe the original AUX sum. Harmonic
stages operate on the gain-reduced audio so extreme settings remain controllable.
The clipper is last in the nonlinear chain and has a known ceiling. The return
remains wet-only because the Slicer AUX architecture already supplies the dry
path.

## Controls

### Dynamics

- `PRESS`: threshold/amount, from no reduction to approximately 24 dB of
  available gain reduction.
- `RATIO`: 1:1 through 20:1, with the upper range behaving as limiting.
- `ATTACK`: 0.1–40 ms; short values flatten hits, longer values preserve kick
  and snare leading edges.
- `RECOVERY`: 20–600 ms with optional host-synchronized values from 1/64 to
  1/2 note. An adaptive mode uses peak-versus-slow-envelope difference rather
  than a fixed release for every slice.
- `SNAP`: bipolar transient gain derived from fast and slow envelopes. This is
  independent of compression amount.
- `SC HP`: 20–300 Hz detector high-pass so kick energy can drive or avoid
  driving the compressor deliberately.

### Color

- `SAT`: symmetric soft saturation for density and RMS lift.
- `BITE`: asymmetric nonlinear contribution for brighter, rougher harmonics;
  its DC component must be removed after the stage.
- `CLIP`: final ceiling/clip depth, independent of SAT and BITE.
- `SHAPE`: continuous round-to-hard transfer-function morph.
- `TILT`: broad post-color dark/bright balance rather than another strip EQ.
- `RETURN`: wet AUX return level.

The first GUI can expose PRESS, SNAP, RECOVERY, SAT, BITE, CLIP, TILT, and
RETURN. RATIO, ATTACK, sidechain high-pass, shape, link mode, and synchronization
belong in an expanded editor. Presets should demonstrate genuinely different
structures rather than only changing gain:

- `CLEAN GLUE`
- `BREAK PRESS`
- `JUNGLE SNAP`
- `PARALLEL SMASH`
- `GABBER CLIP`
- `DIGITAL TEAR`

## Multichannel behavior

Slicer 16 needs an explicit link policy:

- `ALL`: one detector and gain computer drive all active channels. This is the
  safest dynamics mode for encoded spatial material because it preserves
  inter-channel level relationships.
- `PAIR`: adjacent stereo pairs share detection and gain. This is appropriate
  for discrete multichannel stems and replaces the temporary pair-only design.
- `FREE`: independent channels, offered only as a deliberate creative mode.

Compression can be spatial-field safe in `ALL` mode because every channel gets
the same time-varying scalar. Nonlinear waveshaping of Ambisonic components is
not mathematically field preserving even when every lane uses the same transfer
function. Therefore the editor needs a `FIELD SAFE` option that disables SAT,
BITE, and CLIP for 3OA material while retaining linked dynamics, SNAP, linear
tilt, and return gain.

No link mode may fold, exchange, decode, or reorder channels. Every output lane
retains the same sample clock.

## Implementation sequence

1. Add `dsp/s3g_break_bus.h` with the log-domain soft-knee compressor,
   peak/slow detector, adaptive recovery, SNAP, link modes, and meters.
2. Add independently switchable SAT and BITE stages with DC rejection.
3. Implement an ADAA clip/saturator path, benchmark it against 2x and 4x
   oversampling, and select by measured alias rejection and 16-channel CPU.
4. Replace the temporary DrumOverload fields in Slicer state and GUI; old
   preview-state compatibility is not required.
5. Add impulse, stepped-level, swept-sine, channel-link, 3OA field-safe, and
   sample-lock regression tests before installing the new processor.

