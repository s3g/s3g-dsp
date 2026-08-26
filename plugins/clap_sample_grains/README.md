# s3g Sample Grains 2 / 32

Four independently loaded sample lanes feed a non-ambisonic granular
instrument. A shaped or manually edited breakpoint path selects and blends
the source lanes; Scan, Freeze, Cloud, and Slice select source time; and a
separate event layer supplies Ordinary, loudness-Sorter, Stutter, Shrink, and
Doublets patterns.

The four waveforms show the granular process directly. Every sounding grain
publishes its real source span, direction, envelope phase, and lane weight;
window outlines and moving phase dots are drawn over the affected audio, with
matching live event rings in the read-head path scope. The shared operation
names are deliberate, but the engine is not Slicer's mutate engine: Slicer
rearranges slice material; Grains schedules live grain events.

The waveform is deliberately darker than the grain overlay. A faint filled
window accumulates where grains overlap; the elapsed contour is medium gray,
the remaining contour is bright, and the white dot with a dark halo is the
current grain phase. Contour thickness follows lane weight, while luminance
also follows the grain's actual current windowed gain. A contour that crosses
a source-window seam breaks at one edge and continues at the other.

MIDI note height controls source Scan Speed around `Scan Root`; each octave
doubles or halves the scan rate in both Normal and Hertz rate bases. `Scan
Tune` and `Scan Fine` offset that rate relationship. They do not transpose the
audio inside a grain. `Pitch Shift` independently transposes every grain by
±48 semitones without changing source scan speed, event density, or grain
duration, while `Pitch Spray` remains the per-event random offset. Doublets
Amount is the probability of emitting the paired grain, from none at zero to
every event at 100%.

The stereo edition accepts mono or stereo files. `Preserve Origins` keeps a
stereo file's left and right channels on their original sides. `Mono Sum`,
`Left`, `Right`, `Mid`, and `Side` deliberately derive mono material instead.
`Mono Spread` gives every mono or mono-derived grain a stable stereo position;
`Stereo Link` either keeps the two original channels on one grain trajectory
or gives them independent position, pitch-spray, and reverse decisions. The
`Polyphony` menu controls MIDI voice ownership and never changes channel count.

The fixed 32-channel instrument retains `Preserve Field`, which keeps every
source channel on its matching output. Its explicit `Distribute` mode folds
each grain to a mono or stereo object before the shared Sequential, Reverse,
Palindrome, Random, or Random Cycle allocator places it.

Only the 32-channel edition publishes the allocator parameters and exposes the
Motion-style `ROUTING` tab with its categorical `ACTIVE OUT` menu. The stereo
edition has a fixed two-channel output and shows its channel interpretation
controls beside voice, scan rate, and MIDI.

The 1280-by-932 editor gives every slider a full-width track at a consistent
24-pixel row pitch. Source waveforms, the read-head path, and a full-width
Grain Process occupy the left; Source Scan, Grain Source, and Path fill the
right column continuously; Voice or Routing spans both columns as the footer.
It does not show Lanes' selected-lane Timing/Stretch toolbox or a separate
simulated grain graph.

Version 0.4.1 restores MIDI-note control of Scan Speed while leaving Grain
Pitch Shift independent.

Version 0.4 removes the inherited Loop Join, Manual Lane, Jump Slew,
per-lane Speed/Stretch/Nudge, and fixed allocation-clock parameters from the
Grains automation and state contract. Manual paths live entirely in the
breakpoint editor, lane jumps occur only at independently windowed grain
starts, and output allocation always occurs per grain. The trigger menu now
contains distinct Gate, finite one-window One Shot, and Toggle behaviors.

Sources use the Sample-family Project, Link, and Embed storage contract and
accept multi-file drag and drop.
Project copies are content-verified: reloading the same unchanged source, or
loading its existing project-media copy directly, reuses that file rather than
creating another copy. The operation remains pending until the host project
has been saved and supplies a media path.
