# s3g Sample Rings 8

Four independent file-or-live-input slots contribute as many as eight concentric channel rings apiece. Eight read heads move radially through the resulting 32-ring source field while retaining independent angular loop clocks. Free, paired, quad, and Field8 formations determine which heads share radial travel; selected-head Mute/Solo and independent Radial/Angular Reverse controls support direct auditioning and performance reversal.

Angle Amount is bipolar, mirroring relationship order or modulation polarity around the Head Pivot. In Manual mode every head has a signed −4x to +4x rate, including a true 0x stop, while its wrapped phase is presented as −180° to +180°.

Playback Rate is the common master for angular source transport and radial movement. Radial Ratio scales a stable eight-second field cycle rather than borrowing the first loaded file's duration, so replacing or reordering sources does not change the radial path speed.

The title-bar menu supplies eleven factory starting points while retaining the standard user preset Load/Save workflow; edited presets gain a `*` marker. Mode-dependent controls dim, display why they are inactive, and stop accepting GUI gestures without discarding their stored values. The expanded selected-head panel switches between effective read/rate/phase/source information and only those Manual controls currently in authority. Fixed and Manual radial modes no longer advance an inaudible background path phase.

The editor presents radial behavior as Field Path and angular behavior as Head Clocks. Its Master Rate label addresses the shared Playback Rate parameter. Dedicated Play, Pause, Stop, and Sync actions support operation without MIDI: Pause retains position, Stop pauses and returns to the start state, and Sync relaunches without changing transport state.

The CLAP is the active replacement for Processor Loop and Processor Multi Loop. It exposes one paired eight-channel input/output port and one MIDI note input. The first eight channels of each imported file become rings; two-tone muted teal boundary rings separate the populated A–D file bands. Circular source analysis layers sampled peak, RMS-energy, and signed contour information so transients and sections remain recognizable without overpowering the head dots. Live capture records one to eight incoming channels using Replace, Overdub, or Punch.

Sources use the shared Sample-family Project, Link, and Embed contract. Project is the default: its worker copies loaded media into the saved REAPER project's `s3g Samples` directory, reuses content-verified copies, registers them with the exact owning project, and retains a small relative locator. An unsaved project remains pending while playback continues from the external file. Link retains that external path; Embed writes decoded PCM into CLAP state. Pathless live captures remain embedded under every mode.

Build and validate with:

```sh
cmake --preset clap
cmake --build build-clap --target s3g_sample_rings_clap s3g_sample_rings_smoke s3g_sample_rings_clap_smoke
./build-clap/s3g_sample_rings_smoke
./build-clap/s3g_sample_rings_clap_smoke build-clap/plugins/clap_sample_rings/s3g_sample_rings.clap
```

See [the Sample Rings guide](../../docs/sample-rings.html) for the radial path, head formations, angular relationships, capture, MIDI, output projection, and state behavior.
