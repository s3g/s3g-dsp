# s3g Tracker real-REAPER acceptance harness

Contract version 1 runs inside REAPER itself; it complements the mock-host
CLAP smoke test. The harness creates a new project tab, inserts the installed
Tracker and the MIDI-capture JSFX, then checks actual host transport playback,
sample-offset MIDI delivery, seek/restart, loop discontinuities, project save
and reopen, and playback after reopen.

Install the harness:

```sh
./scripts/install-tracker-reaper-acceptance.sh
```

In REAPER, open Actions, load/run
`Scripts/s3g/s3g_tracker_acceptance.lua`, and leave the generated project tab
open until the result dialog appears. The detailed log and generated v1
acceptance project are written to:

```text
/tmp/s3g-tracker-reaper-acceptance.log
/tmp/s3g-tracker-reaper-acceptance-v1.rpp
```

The script never changes an existing project tab. The generated acceptance
tab and `/tmp` artifacts may be discarded after inspection.

The following editor-owned interactions remain a short manual extension to
the automated host pass:

1. Open Tracker, choose `MIDI REC: STEP`, send a controller note, and verify
   NOTE/VOL are written at the cursor and the cursor advances.
2. During pattern playback choose `MIDI REC: LIVE Q`, play across several
   ticks, and verify notes wrap inside the selected NOTE lane length while the
   cursor follows each written NOTE without auto-advancing.
3. During pattern playback choose `MIDI REC: LIVE MT`, play slightly ahead of
   and behind ticks, and verify MT appears in an available SEQ pair.
4. Detach Console, submit Live Code from its repeated entry line, then reattach.
5. Queue Song rows at NEXT TICK, BEAT, CYCLE, and SONG ROW and verify the yellow
   pending marker clears exactly at the named boundary.
6. Trigger PANIC and confirm the capture/receiving instrument retains no notes.

MIDI-file import is intentionally outside the current-version contract.
