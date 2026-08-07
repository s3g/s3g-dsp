# Native project format

`s3g Tracker` projects use the versioned `.s3gt` JSON format. Schema version 2
is native-only: Max patchers, `pattr` state, legacy tracker files, and schema
version 1 files are not compatibility targets.

The document stores an ordered pattern bank, its active stable pattern ID, and
every independent column's length, stride, phase, direction, and mute state;
NOTE/INS/normalized VOL and both FX/value pairs;
transport, loop, swing, timing warps, and deterministic seeds; rack instances
and patches; instrument-owned MIDI routes; MAIN OUT state; sampler file paths
and edited slice tables; per-pattern aliases and lane pitch memory; and the
Song arrangement.
Song transport enablement is stored separately from the arrangement, so Song
rows can be drafted without changing the main Play button from Pattern mode.

Decoded sample PCM and waveform/transient analysis are derived runtime data.
Opening a project reloads referenced mono/stereo files in the background,
recomputes analysis, and republishes the rack without marking the document
dirty. Missing files remain referenced, are reported in the console, and are
retried when the sampler editor is reopened.

Save encodes and validates a complete candidate, writes a same-directory
temporary file, syncs it, atomically renames it over the destination, and syncs
the parent directory. Open decodes transactionally and does not replace the
live session unless the whole document is valid. Unknown object fields are
ignored for forward extension; unknown enum values and incompatible schema
versions fail closed.

File commands are `Command-O`, `Command-S`, and `Shift-Command-S`. Concurrent
Open/Save requests execute in issue order, stale UI completions are ignored,
and edits made after an asynchronous save begins remain marked unsaved. Open,
close, and quit require explicit confirmation before discarding edits.

Schema version 2 stores up to 256 patterns in user-visible order. Stable IDs
use 1–64 ASCII letters, digits, `.`, `_`, or `-`, beginning with a letter or
digit. IDs are independent of editable pattern names, so renaming a pattern
does not break Song rows. The active ID and every Song-row reference must
resolve inside the bank; duplicate or unresolved identifiers fail closed.
Each bank entry owns its console alias map and lane pitch memory. Alias lane
indices are validated against that entry's track count, so switching between
patterns with different lane layouts cannot leave stale reachable aliases.
