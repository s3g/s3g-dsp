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

- **Local floor** — correlated low/mid ground and environmental body, explicitly
  excluding a synthetic recorder-noise floor.
- **Landscape bed** — a correlated low/mid ecology body plus delayed terrain
  or water returns; it contains no raw broadband noise-floor source.
- **Horizon signals** — sparse bells, machinery, aircraft, foghorns, and slow
  synthetic events that emerge from and recede into the bed.

Each entity first generates a mono source using one of nine compact engines:
flow, rolling noise, machinery harmonics, struck modal resonance, air noise,
aggregate road traffic, aircraft, foghorns, or surf. Road traffic combines
detuned combustion orders, correlated tyre/road
body, and independent slow vehicle passages. Aircraft combine turbine roar
with restrained spool/blade orders and follow long moving flight paths whose
elevation, range, closest-approach level, and Doppler shift evolve together.
The aircraft `FLIGHT` state continuously moves this model from landed/taxi
motion through takeoff or approach to a high overhead pass, jointly changing
path width, elevation, passage speed, engine load, closest-approach level, and
Doppler. Foghorns use a nonlinear pressurized piston drive feeding a resonant
air-column/horn body, with a short pressure-dependent pitch grunt at the edges
of each call. Surf is built from overlapping asymmetric wave cycles rather
than a static noise bed.

Generator events use separate target and audible envelopes. Score onsets,
machine activity, and modal strikes are ramped per sample, with attack and
release times increasing with perceptual range. Bells and motor partials also
pass through a distance-scaled transient filter before
outdoor propagation. This preserves recognizable calls and resonances while
preventing close, percussive or click-like attacks from contradicting the
acoustic-horizon perspective.

The shared environmental score creates finite activity/rest arcs, occupancy,
memory, and delayed spatial cascades. Outdoor propagation is then applied per
entity before the final HOA encoding stage. This ordering is important: air
loss, terrain, ground carry, turbulence, and distance-dependent spatial
coherence belong to each source path, not to an already encoded mix.

`SIGNALS` can assign only recognizable discrete or passing generators—modal
resonators, traffic, aircraft, and foghorns—to the sparse score layer. Motors,
Flow, Roll, and Air remain continuous Local Floor or Landscape body, where
score onsets cannot turn them into foreground noise swells or steam-like
exhaust events.

After direct propagation, non-local sources feed a four-direction outdoor
return. This is not an indoor room reverb: its sparse delays represent
separated ground, shoreline, ridge, and irregular-topography paths, while a
dark low-feedback loop supplies the weaker scattered tail. Water and Coast use
longer, lower-elevation paths; land ecologies use shorter, terrain-dependent
paths. The returns are encoded with deliberately low HOA coherence so that a
foghorn or bell can remain locatable while its environmental response spreads
across the horizon.

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
| Generators | Ecology-specific family levels and model controls | Independent synthesis-family levels, each reaching a true zero, plus the parameters relevant to the selected ecology. |
| Environmental score | `MEMORY`, `CASCADE`, `SIGNALS`, `LANDSCAPE`, `LOCAL FLOOR` | Event continuity, outdoor return, and three-layer balance. |
| Horizon | `RANGE`, `AZIMUTH`, `ELEVATION`, `ARC`, `DETAIL`, `EDGE` | Perceptual distance, field placement, angular extent, spatial coherence, and horizon level. |
| Atmosphere | `AIR LOSS`, `AIR NOISE`, `GROUND`, `TERRAIN`, `CARRY`, `TURBULENCE` | Frequency loss, optional synthesized broadband air, surface behavior, occlusion, anomalous reach, and slow/fast amplitude variation. |
| Field listener | `LISTEN`, `AMOUNT`, `RESPONSE` | Directional analysis of the generated HOA field and bounded evolution of audibility, direct/return balance, and apparent-distance cues. |
| Identity | `SEED` | Deterministic scene identity and repeatable state recall. |

The ecology choices are Mixed, Rural, Traffic, City, Industrial, Water,
Weather, Airport, and Coast. Ground choices are Water, Hard, Mixed, Grass, and
Forest. Discrete choices use menus; continuous values use shared family
sliders with double-click reset.

The generator panel is contextual: it shows only source families available to
the selected ecology and replaces irrelevant rows with controls for those
active models. Airport exposes `AIRCRAFT`, `FLIGHT`, `PASS SPEED`, `ENGINE
POWER`, `ENGINE TONE`, `TRAFFIC`, `ROAD SPEED`, and `ENGINE LOAD`. Water and
Coast expose `FOGHORNS`, `HORN PITCH`, `HORN PRESSURE`, `CALL LENGTH`, `SURF`,
`WAVE RATE`, and `WAVE BREAK`. Other ecologies similarly expose the applicable
machine tone, bell pitch/decay, traffic, aircraft, or surf controls.

`FLIGHT` is labeled at musically useful regions—Landed, Taxi, Takeoff,
Approach, and Overhead—but remains a continuous automatable parameter. At its
landed end an aircraft stays close to the horizon and traverses a narrow,
slow path; at its overhead end the path widens, rises steeply, accelerates,
approaches the listener, and recedes with restrained Doppler. `HORN PRESSURE`
changes attack/release, drive saturation, upper partials, and air-column
response together rather than acting as another level control. `HORN PITCH`
sets the piston/horn identity, while `CALL LENGTH` changes the active portion
of the repeating maritime call.

`MACHINES` controls only continuous motor and machinery-harmonic voices. The
former synthetic pressure-release voice has been removed from every ecology,
and motor voices are excluded from the score-gated `SIGNALS` layer. Machinery
now remains continuous distant `BED` body instead of rising through successive
foreground score stages; setting `MACHINES` to zero removes it completely. The
family output is calibrated below the generic source
ceiling so a fully raised Machines control remains part of the horizon rather
than becoming a foreground layer. `BELLS` controls struck modal voices. Their
fundamental and inharmonic upper mode are bounded after pitch transposition;
distance lowers the modal ceiling, and the upper mode decays faster than the
bell body so high settings cannot settle into a persistent whistle or sonar
tone. Modal score gain and strike strength have fixed distant-field ceilings,
and their peak path remains linear rather than using audible saturation, so
overlapping rural bells cannot drive themselves into the foreground. The four
newer families have dedicated engines. An ecology
determines which entities are available, while a family slider determines
whether that family is audible. A zero value therefore removes a family without
requiring a different preset or changing the overall horizon layer balance.
Every family and model control is dezippered for live automation.

The `IDENTITY`/`SEED` panel is placed beneath Field Listener in the right-hand
column, leaving clear bottom margin instead of touching the plugin edge.

`AIR LOSS` is propagation: increasing it removes high-frequency energy with
distance. `AIR NOISE` is source synthesis: it controls only the broadband-air
voices and reaches true zero without disabling environmental motion, distant
events, ground response, or HOA detail. This separation avoids using recorder-
or tape-like hiss as a proxy for outdoor distance.

`LOCAL FLOOR` is also noise-floor independent. Its sources are reconstructed
through correlated low/mid body filters, so raising it adds nearby environmental
presence rather than tape hiss.

`LANDSCAPE` replaces the original broadband-weighted `BED` behavior while
retaining the same parameter ID and saved-state value. It controls a quieter
direct low/mid ecology body and the send into the terrain/seascape return.
Generic Flow, Roll, and Air bed voices are reconstructed from correlated filter
states rather than raw high-frequency excitation. Existing sessions therefore
remain compatible, but factory scenes use lower Landscape values because the
new return contributes depth without requiring a constant noise layer.

## Field Listener

Horizon adapts the shared [Listener Mode](listener-mode.html) concept with an
eight-pickup Cube listener. The tap is the completed pre-output ACN/SN3D field:
direct entities and the four terrain or seascape returns are both present, while
the final `OUT` trim is divided out. The response therefore follows what the
ambisonic scene itself contains rather than a mono sum, GUI telemetry, or the
level used to monitor the plugin.

`LISTEN` selects the directional law. `FOLLOW` prefers recently audible
directions, `COUNTER` evaluates their antipodes, and `BALANCE` prefers less
exposed regions. `OFF` is an exact open-loop reference. The `AMOUNT` control
scales a bounded response; it does not feed audio back into the generator.

The four Horizon responses map the same heard field differently:

- `REACH` makes small directional changes to source audibility, environmental
  return, and apparent range.
- `GLIMPSE` uses short-term novelty, charge, and roughness to reveal a faint
  event without retriggering its synthesis envelope.
- `SETTLE` uses slower habituation to restrain persistently exposed directions
  and move a little energy toward the landscape return.
- `DISTANCE` combines the measured direct-to-return energy relationship with directional
  spectral tilt and habituation, then adjusts propagation loss, HOA coherence,
  direct level, and return balance together.

The last mapping is informed by the modified direct-to-reverberant energy model
for the auditory horizon described by Bronkhorst and Houtgast, while its
frequency-dependent interpretation follows the role of atmospheric absorption
formalized by ISO 9613-1. The slow directional memory and novelty terms treat
the environment as an evolving spatio-temporal soundscape rather than a static
noise spectrum. These sources are listed under
[Outdoor Sound Propagation and Auditory Distance](references.html#outdoor-propagation).
The response remains perceptual and bounded; it is not a calibrated distance
estimator or a Harmonoise propagation solver.

## Factory scenes

The preview supplies sixteen deterministic scenes: Bell Across Valley,
Clear Night Inversion, Highway Beyond Fields, City Beyond Ridge, Industrial
Night Reach, Across Still Water, Dawn Long Horizon, Storm Beyond Hills,
Distant Rail Corridor, Quiet Agricultural Basin, Settlement Through Forest,
Vanishing Acoustic Horizon, High Fidelity Horizon, Airport Approach Corridor,
Foghorns Beyond Headland, and Open Ocean Beyond Dunes. The High Fidelity scene
sets `AIR NOISE` to zero while retaining the synthesized environmental field.

Factory selection, custom load, and randomization preserve `OUT` and `ORDER`.
Camera mode, azimuth, elevation, and zoom are serialized with the plugin state.
Version-1 through version-4 states/custom presets migrate into the version-5
listener surface. The fourteen model controls and three listener controls are
append-only, so existing parameter IDs do not move. Factory selection and
randomization preserve the current listener mode, amount, and response as well
as `OUT` and `ORDER`.

`RANDOM` does not select or lightly perturb a factory scene. It creates a new
bounded ecology-aware parameter set, selects compatible source families,
ground, propagation, and model ranges, assigns a new `SEED`, and displays
`RANDOM` as the preset name. The non-factory name survives project state recall;
the host-facing factory-preset parameter remains inside its declared range.

## Research directions for later versions

The first implementation leaves space for physically tighter models without
changing its three-layer architecture. Supporting literature is collected under
[Outdoor Sound Propagation and Auditory Distance](references.html#outdoor-propagation)
and, for source resonators and environmental synthesis,
[Modal Synthesis and Physical Modeling](references.html#modal-physical-modeling)
on the shared reference page:

- ISO 9613-1/-2 atmospheric absorption and favorable-propagation profiles.
- Ground-effect interference models such as Delany–Bazley/Miki impedance and
  Nord2000/Harmonoise propagation over mixed terrain.
- Temperature inversions, wind-gradient shadow zones, and terrain diffraction.
- Auditory scene analysis of low-SNR event detection, temporal glimpsing, and
  distance perception from spectral, direct-to-reverberant, and modulation cues.
- Measured long-range rural, highway, rail, marine, and urban-edge corpora for
  perceptual calibration rather than sample playback.

These are calibration and extension paths; the preview remains fully synthetic
and requires no loaded sound files.
