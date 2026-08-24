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
a loop or wrap-nudge seam breaks at one edge and continues at the other.

The stereo and fixed 32-channel instruments share session state and controls.
`Preserve Field` keeps every source channel on its matching output. The
explicit `Distribute` mode folds each grain to a mono or stereo object before
the shared Sequential, Reverse, Palindrome, Random, or Random Cycle allocator
places it. This makes output topology independent of source-path and grain
event choices.

Only the 32-channel edition exposes the Motion-style `ROUTING` tab and its
categorical `ACTIVE OUT` menu. The stereo edition keeps that topology fixed
and shows only its voice, pitch, and MIDI page.

The 1280-by-908 editor gives every slider a full-width track at a consistent
24-pixel row pitch. Source waveforms, the read-head path, and a full-width
Grain Process occupy the left; Source Scan, Grain Source, and Path fill the
right column continuously; Voice or Routing spans both columns as the footer.
It does not show Lanes' selected-lane Timing/Stretch toolbox or a separate
simulated grain graph.

Legacy per-lane timing values remain state-compatible and host-automatable,
but they do not occupy the Grains editor. Sources use the Sample-family
Project, Link, and Embed storage contract and accept multi-file drag and drop.
