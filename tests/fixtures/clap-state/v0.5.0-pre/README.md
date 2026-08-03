# CLAP state fixtures from 0.5.0-pre

These are deterministically seeded, non-default states exported from the signed
`0.5.0-pre` macOS release bundles. Each `.params.tsv` sidecar records the old
bundle's parameter ID/value snapshot. The current CLAPs must load each released
state, reproduce every released parameter value within serialization precision,
expose finite in-range parameters, save a migrated state, reload it without
changing any current parameter, and produce identical bytes on a second save.

The fixture set deliberately covers the direct panners, the high-cost Water and
Insect generators, evolving encoder state, both Spectral Topology widths, and
Fault's larger state schema. Add fixtures when a released stateful family gains
a migration path; do not regenerate an old-version directory from current
binaries. The fixture generator mode is `s3g_clap_state_fixture_smoke --seed`;
always point it at the archived release bundle named by this directory.
