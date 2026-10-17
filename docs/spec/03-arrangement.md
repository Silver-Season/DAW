# 03. Arrangement view

## Goal

Add a linear timeline on which clips are assembled into a track from start to finish. The same tracks, devices and mixer as in the Session view; `Tab` switches views. Session and Arrangement are connected: you can play clips in the Session and record that onto the timeline.

At the end the user can: play scenes in the Session view with recording on and get them on the timeline, switch with `Tab`, place "Verse" and "Drop" markers, loop a section, drag and split clips, record a part directly onto the timeline, and pull a track from an old version of the project.

## Scope

Included: the arrangement model, playback, timeline and ruler, markers, loop, cursor and time selection, clip editing, snapping and zoom, MIDI recording directly onto the timeline, recording the session into the arrangement, session priority over arrangement and "Back to Arrangement", inserting and deleting time, "Pull Track from Version".

Not included: automation lanes (06), audio clips (05), tempo track and time signature changes (07), export (04).

---

## Task 3.1. Model

```
TRACK
└── ARRANGEMENT
    └── ACLIP  id  start  length  (beats from the start of the song)
               offset=0     (beats inside the source clip where it starts)
               loop=1       (repeat the clip content if the ACLIP is longer than the clip loop)
               └── CLIP …   (its own copy of the content, same schema as in a slot)
PROJECT
├── MARKERS
│   └── MARKER  id  beat  name
└── arrangementLoopStart=0  arrangementLoopEnd=16  arrangementLoopOn=0
    arrangementPlayStart=0  (where the space bar plays from)
    followPlayhead=1
```

- ACLIPs do not overlap on one track. Any operation that creates an overlap trims or splits the underlying clip: the new clip "covers" the old one. A `resolveOverlaps (track, newClipId)` function in the model, with tests.
- A clip moved from the session to the timeline copies its content (rather than referencing the slot).
- Song length = the end of the last ACLIP or the last marker, at least 16 bars, plus 8 bars of empty space in the UI.

## Task 3.2. Engine: arrangement playback

- `PlaybackGraph::Track` gets an array sorted by `start` of `ArrangementClipPlayback { startBeat, endBeat, offsetBeats, loop, shared_ptr<const MidiClip> }`.
- `core`: `renderArrangement (clips, blockStartBeat, blockEndBeat, samplesPerBeat, numSamples, out)`. For each ACLIP intersecting the window, the content is rendered taking into account `offset`, the loop and trimming at `endBeat`. At the end of an ACLIP all sounding notes get a note-off (the same `held` mechanism as in `ClipPlayer`).
- Seeking (clicking the ruler during playback): all currently sounding notes are turned off, and from the new position the notes **that start before the position and end after it** start sounding (chase notes). This matters for long pads.
- Arrangement loop: when the position reaches `loopEnd`, the transport jumps to `loopStart` within the same block, and the block is split into two parts. `Transport` in core gets `setLoop (start, end, on)` and produces up to two windows per block. A test for a sample-accurate jump.

### Session vs. arrangement

- A track has two sources: arrangement and session. Launching a clip in the session puts the track "in session": its ACLIPs go silent, and the track in the Arrangement shows a gray bar over its clips.
- The "Back to Arrangement" button (orange) in the transport lights up when at least one track is in session. Pressing it stops the session clips at the next quantized boundary and returns all tracks to the arrangement.
- Stopping a clip with the track's "stop" button also leaves the track "in session" (silent) until "Back to Arrangement".

## Task 3.3. Arrangement view UI

`ui/ArrangementView` takes the place of `SessionView` in `MainView`. `Tab` switches views with a smooth 150 ms transition (if animations are enabled).

### Layout

```
┌ track     ┬ marker ruler (names, click to jump) ────────────────────────────────┐
│ headers   ├ bar ruler (click: cursor, drag: selection, loop) ───────────────────┤
│ (as in    ├──────────────────────────────────────────────────────────────────┤
│ session:  │  tracks with clips                                                │
│ name, M S ●│                                                                  │
│ volume)   │                                                                   │
├───────────┴──────────────────────────────────────────────────────────────────┤
│ whole-song overview (minimap, frame of the visible area)                     │
└──────────────────────────────────────────────────────────────────────────────┘
```

- Track header: color, name, on/S/● buttons, volume as a number (draggable with the mouse), pan. Track height 24–240 px, resized by dragging the bottom edge; `Alt+wheel` over a track changes its height; `H` (with focus in the Arrangement) toggles "all tracks the same height / by content".
- The minimap at the bottom, 36 px high: all clips of the song in miniature, the frame of the visible part can be dragged and resized by its edges to change the zoom.
- Clips are drawn as in the design concept: track color, name, notes in miniature (or bars if the clip is very narrow), a loop icon if the content repeats.
- The playhead is a "beam": a `text`-colored line 1.5 px wide with an `accent` glow when `glow > 0`; where the beam crosses a clip, six thin colored spectrum bars are drawn to its right (see `drawArrangement` in the concept). The beam is drawn as a separate lightweight component on top, so the tracks are not redrawn 60 times per second.
- "Follow playhead" (a transport button, `Cmd+Shift+F`): when the playhead goes past the right edge, the view pages forward.

### Time, selection, loop

- Clicking the bar ruler sets the start point (`arrangementPlayStart`) and, if playing, seeks. Dragging on the ruler or on empty track space selects a time range (on those tracks).
- Loop: a bracket above the ruler. `Cmd+L` sets the loop to the selection. Clicking the bracket toggles the loop on and off, its ends and the whole bracket can be dragged with the mouse.
- Space plays from `arrangementPlayStart`, `Shift+Space` continues from where it stopped. A double stop (pressing ■ with the transport stopped) returns the cursor to the start of the song.
- `Home` to the start, `End` to the end, `←/→` moves the selection by a grid step.

### Markers

- Above the ruler is a marker strip. The "+ Marker" button (or `Cmd+M`) places a marker at the cursor position. Double-clicking the name renames it, dragging moves it, `Delete` with a marker selected deletes it.
- The ◀ ▶ buttons next to "+ Marker" jump to the previous and next marker. Clicking a marker during playback jumps to it at the next quantized boundary.

### Grid, snapping, zoom

- The grid is adaptive to zoom (lines no closer than 12 px) or fixed, `Cmd+1/2/3/4` as in the piano roll.
- Clips and clip edges snap to the grid; holding Cmd while dragging disables snapping.
- Zoom: `Cmd+wheel`, pinch, `+`/`−` (with focus in the Arrangement), `Z` zooms to the selection, `X` returns to the previous zoom. Vertical scrolling with the wheel, horizontal with Shift+wheel or a gesture.

### Clip editing

| Action | How |
|---|---|
| Move | Drag a clip (between tracks too, if the track type matches) |
| Copy | Alt+drag, `Cmd+C`/`Cmd+V` at the cursor position, `Cmd+D` duplicates the selected time or clip right after it |
| Length | Drag the right edge: longer than the loop repeats the content; dragging the left edge changes `offset` |
| Split | `Cmd+E` at the cursor or at the edges of the time selection |
| Join | `Cmd+J`: selected clips on one track become one clip (notes are carried over, gaps remain) |
| Delete | `Delete` removes clips or the content of the selected time |
| Insert time | `Cmd+I` with a time selection inserts empty time of the same length on all tracks, everything to the right shifts (in the Session view `Cmd+I` inserts a scene) |
| Delete time | `Cmd+Shift+Delete` deletes the selected time on all tracks with a shift |
| Draw a clip | Double-click on empty space of a MIDI track: an empty 1-bar clip |
| Open | Double-click on a clip opens the piano roll (02) for its content |
| Rename / color | `Cmd+R`, right-click → color |
| Send to Session | Right-click → "Send to Session": a copy of the clip into the first empty slot of the track starting from the selected scene |

- From the Session view to the Arrangement: dragging a clip onto the `Arrangement` button in the transport switches the view and continues the drag onto the timeline.
- All operations are undo transactions, all overlaps are resolved by `resolveOverlaps`.

## Task 3.4. Recording

### Directly onto the timeline

- The ● button in the transport with the Arrangement open (or `F9`) turns on arrangement recording. On all armed MIDI tracks a growing ACLIP is created from the cursor position, notes are drawn as they are played. Stopping the transport or pressing ● again ends recording. Count-in and metronome as in 02.
- With the arrangement loop on: each pass becomes a separate **take** (in this stage: a new ACLIP on top of the old one, while the old passes are kept in `ACLIP/TAKES` and chosen via right-click → "Takes ▸ Take 1 / 2 / 3"). Full comping with take lanes comes in 05.
- Overdub (OVR button) on the timeline: new notes are added to the existing clip under the cursor rather than replacing it.
- Punch-in / punch-out: two buttons in the transport next to the loop. When on, recording happens only inside the loop bracket.

### Session → arrangement

- If ● is pressed with the Session view open while the transport plays from the arrangement, everything launched in the session is recorded onto the timeline: each clip launch creates an ACLIP with a copy of its content from the moment of launch to the stop or clip change. Live notes on armed tracks are recorded too.
- Implementation: the message thread keeps a log of launch/stop events from the engine (`EngineEvent { beat, trackId, slot, type }`, an SPSC queue from the audio thread) and after recording stops turns the log into ACLIPs in one transaction.

## Task 3.5. Pull track from version

- The version list (01, task 1.7) gets a "Pull Track…" button. It loads the selected version into memory (not into the engine), shows the list of its tracks, and the chosen track with all its devices, session clips and arrangement clips is added to the current project as a new track (one transaction, `id`s are reissued). Name: "Reese (v12)".

## Tests

1. **Overlaps.** `resolveOverlaps`: a clip on top of the middle of another splits it in two; on top of an edge trims it; fully covering removes it. Undo restores everything.
2. **Arrangement render** (core): a 1-bar clip with `loop=1` in a 4-bar ACLIP gives 4 repetitions of the notes; `offset` = 2 beats shifts the content; at the end of the ACLIP all notes are closed.
3. **Chase notes.** Seeking into the middle of a 4-bar pad note produces a note-on for that note in the first block after the seek.
4. **Transport loop** (core): a 4-bar loop at 120 BPM and 48 kHz: the jump happens at sample 384,000 ± 0 within the block, the block is split into two windows, the total length of the windows equals the block size.
5. **Session vs. arrangement.** Launching a session clip silences the track's ACLIPs; "Back to Arrangement" brings them back at a quantized boundary.
6. **Recording onto the timeline.** Fake events over 2 bars from position 9.1.1 create an ACLIP with `start = 32` beats and notes in the right places.
7. **Session → arrangement.** A sequence of scene launches (Intro 8 bars, Drop 8 bars) gives each track ACLIPs of the right length with copies of the clips.
8. **Inserting and deleting time** shift the clips and markers of all tracks; an ACLIP split by an insertion becomes two.
9. **Pull track.** A track with the same content and new `id`s, with no conflicts with the current ones, is added from a saved version.

## Done when

- [ ] `Tab` switches between Session and Arrangement, tracks and mixer are shared (Arrangement screenshots in 4 themes, the playhead beam is visible).
- [ ] A track structure is recorded from scenes onto the timeline, and then plays without the session.
- [ ] Markers, loop, punch-in/out and follow playhead work.
- [ ] Every action in the clip editing table works with mouse and keys, undo one action at a time.
- [ ] MIDI recording directly onto the timeline and takes over the loop work.
- [ ] A long note is chased on seek, there are no hanging notes at clip ends.
- [ ] A track can be pulled from an old version.
- [ ] All tests green, CI green, README updated.
