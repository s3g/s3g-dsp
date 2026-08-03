# s3g Ambi Encoder Horizon — future-release preview

`s3g Ambi Encoder Horizon` is an autonomous, zero-input 1OA–7OA synthesis
encoder for the audible edge of an outdoor sound field: isolated signals that
carry for kilometers in a quiet rural environment, or the faint continuous
traffic and city bed heard beyond the immediate scene.

This component is **not part of the 0.6.0-pre release**. Its DSP smoke test and
CLAP target are disabled by default, it is absent from `scripts/clap-bundles.tsv`,
and it is therefore excluded from the 0.6.0-pre package and release gates.

## Source-build preview

```sh
cmake -S . -B build-horizon \
  -DCMAKE_BUILD_TYPE=Release \
  -DS3G_BUILD_CLAP_PLUGIN=ON \
  -DS3G_BUILD_FUTURE_COMPONENTS=ON \
  -DS3G_ENABLE_REALTIME_ALLOC_PROBE=ON
cmake --build build-horizon \
  --target audit_ambi_horizon_future
ctest --test-dir build-horizon -R s3g_ambi_horizon_encoder_smoke
```

The Horizon audit covers 48/96 kHz at 16, 32, 64, and 256 frames with
same-sample and distributed automation bursts. When the allocation probe is
enabled it also requires zero allocation or deallocation in every measured
callback. A dedicated worst-case check holds 7OA and 32 entities for 2,000
blocks at 96 kHz/16 frames and fails if sustained p95 callback load reaches
90% of the audio deadline.

On macOS the bundle is written to
`build-horizon/plugins/clap_ambi_horizon_encoder/s3g_ambi_horizon_encoder.clap`.
The CLAP has no audio input and exposes a fixed 64-channel ACN/SN3D output bus;
`ORDER` chooses how many leading channels contain the active 1OA–7OA field.

## Model

The renderer owns up to 32 independent entities and separates the scene into
three audible layers:

- **Local floor** — close air, ground, and low-level environmental activity.
- **Horizon bed** — diffuse traffic, settlement, water, wind, and low machinery.
- **Horizon signals** — sparse modal, motor, air, and pulse events that emerge
  from and recede into the bed.

Each entity first generates a mono source using one of six compact engines:
flow, rolling noise, motor harmonics, struck modal resonance, air noise, or
pulsed events. The shared environmental score creates finite activity/rest
arcs, occupancy, memory, and delayed spatial cascades. Outdoor propagation is
then applied per entity before the final HOA encoding stage. This ordering is
important: air loss, terrain, ground carry, turbulence, and distance-dependent
spatial coherence belong to each source path, not to an already encoded mix.

`RANGE` is a perceptual kilometer scale from 0.03–20 km rather than a calibrated
sound-pressure prediction. Pure inverse-square attenuation would erase the
very condition being modeled, so the renderer combines restrained geometric
loss with frequency-dependent propagation and `CARRY` while preserving faint
audibility.

## Parameter surface

| Group | Parameters | Purpose |
| --- | --- | --- |
| Output | `OUT`, `ORDER` | Master gain and active HOA order. |
| Ecology | `ECOLOGY`, `ENTITIES`, `ACTIVITY`, `OCCUPANCY`, `PACE` | Source-family weighting and population behavior. |
| Environmental score | `MEMORY`, `CASCADE`, `SIGNALS`, `BED`, `LOCAL FLOOR` | Event continuity, propagation, and three-layer balance. |
| Horizon | `RANGE`, `AZIMUTH`, `ELEVATION`, `ARC`, `DETAIL`, `EDGE` | Perceptual distance, field placement, angular extent, spatial coherence, and horizon level. |
| Atmosphere | `AIR`, `GROUND`, `TERRAIN`, `CARRY`, `TURBULENCE` | Frequency loss, surface behavior, occlusion, anomalous reach, and slow/fast amplitude variation. |
| Identity | `SEED` | Deterministic scene identity and repeatable state recall. |

The ecology choices are Mixed, Rural, Traffic, City, Industrial, Water, and
Weather. Ground choices are Water, Hard, Mixed, Grass, and Forest. Discrete
choices use menus; continuous values use shared family sliders with
double-click reset.

## Factory scenes

The first preview supplies twelve deterministic scenes: Bell Across Valley,
Clear Night Inversion, Highway Beyond Fields, City Beyond Ridge, Industrial
Night Reach, Across Still Water, Dawn Long Horizon, Storm Beyond Hills,
Distant Rail Corridor, Quiet Agricultural Basin, Settlement Through Forest,
and Vanishing Acoustic Horizon.

Factory selection, custom load, and randomization preserve `OUT` and `ORDER`.
Camera mode, azimuth, elevation, and zoom are serialized with the plugin state.

## Research directions for later versions

The first implementation leaves space for physically tighter models without
changing its three-layer architecture:

- ISO 9613-1 atmospheric absorption and meteorological refraction profiles.
- Ground-effect interference models such as Delany–Bazley/Miki impedance and
  Nord2000/Harmonoise propagation over mixed terrain.
- Temperature inversions, wind-gradient shadow zones, and terrain diffraction.
- Auditory scene analysis of low-SNR event detection, temporal glimpsing, and
  distance perception from spectral, direct-to-reverberant, and modulation cues.
- Measured long-range rural, highway, rail, marine, and urban-edge corpora for
  perceptual calibration rather than sample playback.

These are calibration and extension paths; the preview remains fully synthetic
and requires no loaded sound files.
