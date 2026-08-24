# s3g Sample Lanes 2

Four-file stereo Sample-family instrument. A normalized read head loops
seamlessly inside shared Start/End bounds while a second, shaped trajectory
moves continuously or jumps between the four source lanes.

The default `Down / Linear / Crossfade` path is a straight diagonal from the
start of lane 1 to the end of lane 4. `Steps` and `Random` paths with `Jump`
produce discrete file changes; Lane Slew makes those changes click-safe.
Random is one deterministic, seeded eight-step path shared by the scope and
audio engine, so the read head always follows the route that is drawn.

`Manual` turns the path scope into a breakpoint editor. Click empty space to
add a point, drag any point to reshape the path, and right-click an interior
point to delete it. The two endpoints remain on the beginning and end of the
path clock but can move vertically. Manual is a direct clock path, so Shape,
Cycles, and Offset are bypassed while it is selected. Up to 16 points are
stored with the plug-in state. Sessions from versions 0.1 and 0.2 retain their
former constant Manual Lane value until a breakpoint is added.

Each lane accepts a mono or stereo file of any duration supported by the host
decoder. Link, Project, and Embed storage apply to all four lanes.

## Model

- X in the central scope is the shared path clock. At default lane timing it
  is also the normalized loop position. Each waveform publishes its lane's
  actual playhead when independent timing moves it away from that clock.
- Y is the file-lane position. Crossfade makes a continuous equal-power blend
  between neighboring loaded lanes; Jump chooses one lane and uses Lane Slew
  to suppress discontinuities.
- Empty lanes do not create silent holes. The read head resolves to the nearest
  loaded lane, and a blend spans any empty lanes between two loaded sources.
- Loop Join overlaps the outgoing endpoint with the matching incoming segment
  in Forward and Reverse transport. Ping Pong reverses at the endpoint and
  therefore does not need a wrap join.

## Per-lane timing

Click a waveform row to expose that lane in the Lane Timing strip.

- **Speed** multiplies the lane read rate. As with tape or ordinary sample-rate
  playback, it changes both duration and pitch.
- **Stretch** divides the lane transport rate while a dual-window overlap
  reader retains the pitch established by Speed and MIDI. `2.00x` therefore
  takes twice as long without dropping an octave; `0.50x` takes half as long.
- **Wrap Nudge** rotates the lane contents by up to half of the selected loop.
  Positive values advance the contents and negative values delay them. Both
  directions wrap seamlessly inside the shared Start/End bounds. The waveform
  visibly rotates beneath the fixed Start/End markers and timeline playhead;
  the dashed marker is the point where the nudged content wraps.

The path clock and the four lane clocks are intentionally separate. A path can
continue crossing lanes at a stable rate while the source material in each row
is independently aligned, stretched, or offset.

Drop one file onto a row to replace that lane. Dropping several files together
fills consecutive lanes from the destination row. Clicking the path scope
switches to Manual and places the read head on the clicked lane height.

Normal rate is anchored to the native duration and sample rate of the first
loaded lane; `1.00x` is ordinary playback at the root note. Hertz expresses
complete shared loop cycles per second. Root, Tune, Fine, and incoming note
pitch then scale either basis.

The editor uses a wide two-column layout: sample lanes and the path scope stay
on the left, selected-lane timing, loop transport, and path controls form the
right toolbox column, and Voice or 32-channel Routing is a compact footer.
