// Adapted verbatim from the original Cocoa help document; parity tests compare the full text.
const std::vector<HelpGuide> helpGuides {
    { "TRACKER GRID WORKFLOW",
        {
            { "COLUMNS",
                "Compact lanes show NOTE and VOL. EXPAND DETAIL reveals SEQ1, V1, SEQ2, V2, and "
                "GATE. Double-click a length to enter a stride such as 24x2; double-click READ to "
                "set its one-based starting row." },
            { "NOTE DISPLAY",
                "NOTE: NAME shows pitches such as C-4. NOTE: MIDI shows the same stored pitch as "
                "decimal value 60." },
            { "CELL SYMBOLS",
                "NOTE: --- rest, RPT retrigger previous, HLD continue the active note, KIL kill, "
                "B01–B64 reusable project-wide sub-row Burst. VOL: DEF default. VOL/SEQ: PRV "
                "previous." },
            { "QUICK ENTRY",
                "In NOTE, X toggles an anchored hit; R writes RPT; H writes HLD; K writes KIL. "
                "Delete clears the active cell or drag selection. [ and ] adjust VOL/V values. M "
                "toggles the selected column mute." },
            { "SEQUENCING + VALUES",
                "Right-click SEQ1/SEQ2 to choose an action or CC, or double-click and type its "
                "code. VOL and sequence values use 0.000–1.000; CC also accepts 0–127. An MT value "
                "may be a plus-separated stack aligned with the NOTE voices; one MT value "
                "broadcasts. Other SEQ actions remain lane-wide. STP holds values; LIN emits "
                "bounded intermediate CC values." },
            { "SELECTION + HISTORY",
                "Drag cells for a rectangle. Control-Shift-Arrows extends a keyboard rectangle. "
                "Control-A/C/X/V selects all, copies, cuts, and pastes. Control-Z and "
                "Control-Shift-Z undo and redo Tracker states; Command-Z remains REAPER's." },
            { "NAVIGATION",
                "Left/Right moves between fields; Up/Down moves by JUMP; Shift-Left/Right moves "
                "lanes. Control-Option-1…0 sets JUMP 1…10. Page Up/Down, Home/End, and F9–F12 jump "
                "through the pattern. Control-=/−/0 changes zoom. < and > switches panels." },
            { "PITCH SHORTCUTS",
                "Control-Option-Up/Down transposes one selected NOTE column by a semitone; add "
                "Shift for an octave." },
            { "PHRASES",
                "Select 2–64 rows in one Tracker lane, then right-click and choose PHRASE > "
                "CAPTURE SELECTION; Control-Shift-P is the shortcut. Referenced Bursts retain "
                "their project-wide slot identity. Control-P copies the selected Phrase at the "
                "cursor. The Phrase page uses Tracker's own inline NOTE/VOL/SEQ/GATE entry, with "
                "VOL shown as normalized 0.000–1.000. The optional Phrase Library BPM field stores "
                "a recommended 20–400 BPM authoring tempo; it never changes host or project "
                "playback. Drag cells or Shift-click within a column to select a range; "
                "Control-Shift-Arrows extends it. Control-A/C/X/V selects all, copies, cuts, and "
                "pastes compatible Phrase cells while retaining their row/column shape. "
                "Right-click SEQ1/SEQ2 for the action and MIDI CC menu; right-click a CD value for "
                "named conditions. PREVIEW CH routes stopped-transport auditioning without "
                "changing the destination lane, while the green playhead advances at project BPM. "
                "Placement copies cells directly and creates no hidden link." },
            { "ASSEMBLE",
                "Choose a Phrase and press A or APPEND to build a vertical Phrase chain. REPEAT "
                "paints 1×–16× copies. Shift-click selects multiple blocks; drag reorders them, "
                "Option-drag duplicates them, and Delete removes them. Preview either the selected "
                "Phrase or the complete Assembly at project BPM. PLACE IN PATTERN materializes "
                "ordinary Tracker cells using Replace or Merge Empty and Extend, Crop, or Wrap "
                "fitting. Assemblies of 2–64 rows can be saved into Project Phrases." },
            { "LANE ORDER",
                "Right-click a lane name at the top of Tracker for Move Lane Left, Right, or To. "
                "The complete lane moves with its column cycles, defaults, aliases, MIDI channel, "
                "record arm, and SEQ data; Song mute assignments follow the musical lane. The "
                "selection transform called Swap Selected Cells affects only the selected cell "
                "rectangle." },
            { "DIRECTIONS + LOOP",
                "FORWARD (>), REVERSE (<), PALINDROME (<>), RANDOM (header: RND). Drag the fixed "
                "row-number gutter or use Shift-Up/Down to set the global loop region." },
        } },
    { "MIDI + LANE ROUTING",
        {
            { "ROUTING",
                "Tracker exposes one CLAP MIDI output and one record input. CH01–CH16 sets each "
                "lane's output channel. Use another Tracker instance for more than 16 "
                "destinations. Double-click a lane name to rename it." },
            { "MIDI RECORD",
                "REC LANE fixes the recording destination independently of the editing cursor. OFF "
                "disarms. STEP collects held keys into one NOTE/VOL chord and advances by View "
                "JUMP once the last key is released. LIVE Q groups notes arriving on the same row, "
                "then writes shared HLD rows and KIL release. LIVE MT writes a pitch-aligned MT "
                "value stack, so every recorded chord voice keeps its own measured onset offset "
                "and attack velocity. Live modes require REAPER playback and pattern transport." },
            { "NOTE + VOL + GATE STACKS",
                "Double-click NOTE to enter up to eight plus-separated pitches, for example "
                "60+64+67. VOL and GATE may contain one value broadcast to every pitch or per-note "
                "values in the same order. GATE accepts DEF, a row duration from 0.01 through 64, "
                "and TIE. If fewer values are entered, the final value repeats; extra values are "
                "ignored." },
            { "ALIASES",
                "aliases lists bindings by lane. alias name 3 assigns or reassigns @name. "
                "autoalias rebuilds the map with the shortest available prefix of each lane "
                "name." },
        } },
    { "TRANSPORT + SONG",
        {
            { "TRANSPORT",
                "Tempo follows REAPER. The labeled BPM menu selects 1/4×, 1/2×, 2/3×, 1×, 3/2×, "
                "2×, or 4×. BPM, Swing, Gate, Loop In/Out, Record mode, and Record lane use "
                "centered labels above one aligned value row, with separators between groups; the "
                "strip scrolls in a narrow host window. Space plays/pauses; Shift-Space toggles "
                "loop. SYNC ALL restarts every lane and column at row 1 without moving REAPER." },
            { "SONG ROW LENGTH",
                "TICKS is the number of tracker-row advances in one pass. FULL / 1× always selects "
                "the complete longest pattern column, or the active Loop In–Out span. Fixed "
                "musical intervals extend through 256 ticks. REP adds passes while column phase "
                "continues." },
            { "SONG BPM ×",
                "Each Song row multiplies the observed REAPER BPM by 1/4×, 1/2×, 2/3×, 1×, 3/2×, "
                "2×, or 4×. This row ratio also composes with the main Tracker BPM multiplier and "
                "changes at the Song-row boundary." },
            { "SONG ROW EDIT",
                "ADD inserts after the selection. DUP makes an exact copy. The ↑ and ↓ controls "
                "move the selected row. Drag the grip beside a row number to move it, or "
                "Option-drag to duplicate it. Stable row identity keeps live reordering aligned "
                "with the row that is actually sounding. Arrangement edits remain available while "
                "REAPER runs; Tracker hands the updated arrangement to playback safely at the next "
                "Song-row boundary without creating a selected-row queue." },
            { "SONG ENERGY",
                "EN % sets the intensity of each Song row. Tracker SEQ action EN plays a note or "
                "Burst only when the row energy meets its threshold; ordinary Pattern playback "
                "supplies 100%. Use different EN thresholds across a pattern to reveal "
                "progressively denser versions from one pattern." },
            { "SONG QUEUE",
                "Select a target row, choose NEXT TICK, NEXT BEAT, END OF PASS, or END OF ROW, "
                "then SELECT QUEUE. END OF ROW is the default and waits through all repetitions. "
                "Queue remains available while REAPER runs with Song Transport on, whether LOOP "
                "SONG is on or off, and can relaunch after a non-looping Song ends. The pending "
                "row is yellow; pattern, warp, swing, and mutes switch together." },
            { "LOOP SONG",
                "LOOP SONG can be switched on or off while REAPER is running. The current Song row "
                "keeps its position; the new loop rule takes effect when playback reaches the end "
                "of the arrangement." },
            { "SONG FILE",
                "SAVE/LOAD SONG + PATTERNS uses one complete validated .s3gt project file." },
            { "WARPS",
                "Compose EXP, STEP, and EUCLID serially; SAVE/RECALL named project slots. During "
                "playback the diagram traces the active curve and marks its current cycle step; "
                "Song WARP follows the saved slot selected by the sounding row." },
        } },
    { "GEOMETRY + TOOL WINDOWS",
        {
            { "GEOMETRY",
                "ACTIVE PULSES shows authored notes; ALL STEPS is the reference; PHASE SPOKES "
                "shows live position; LANE FOCUS isolates a lane; COMPOSITE RING normalizes "
                "cycles. Pattern and active Song-row NOTE mutes retain their fixed ring positions "
                "as dark dashed M placeholders; their pulses and playheads are hidden." },
            { "TOOLS + LIVE CODE",
                "Geometry, Warps, Console, and Help detach with ↗ or a double-click on the page "
                "tab. Detached Console keeps its Live Code line. Press : or backtick to focus Live "
                "Code; Escape returns to the grid." },
            { "HOST SAFETY",
                "Command-key combinations are not claimed by Tracker and remain available to "
                "REAPER. ? opens Help." },
            { "READING HELP",
                "Use the scroll bar to navigate. All text is selectable and copyable." },
        } },
};
