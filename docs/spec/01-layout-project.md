# 01. Project model, new window, browser, themes, saving

## Goal

Turn the phase 0 skeleton into a real application: a project with any number of tracks and scenes that saves, opens, undoes any action, and never loses work. The window gets a four-zone layout: transport at the top, browser on the left, Session View in the center, detail panel with the device chain at the bottom. Four visual themes and full audio interface settings appear.

At the end the user can: open the DAW, drag a Prisma preset from the browser onto an empty area (a track is created), add a third-party effect to the track, save their own preset, save the project, close it, reopen it, and get everything back as it was, including the sound of every plugin.

## Scope

In scope: a `ValueTree` model, undo and redo, a lock-free engine with dynamic tracks, the project file, autosave and crash recovery, version history (list, labels, restore), the window layout, the browser, drag and drop, the device chain (Prisma and third-party AU/VST3 instruments and effects), user presets for devices and chains, themes and fonts, the settings window (audio, MIDI, appearance), menus and keyboard shortcuts, the hint bar.

Out of scope (later): recording and the piano roll (02), Arrangement (03), export and version preview (04), audio tracks and samples on tracks (05), built-in effects, sends, groups, automation (06), the 16-pad drum machine and the sound library (08). Model fields for these features are created where stated below, but no logic is written.

---

## Task 1.1. Project model

Create `app/Source/model/` with the classes `ProjectModel`, `Ids.h` (all `juce::Identifier`s), and the wrappers `TrackRef`, `ClipRef`, `SceneRef`, `DeviceRef` (lightweight classes over `ValueTree` with typed getters and setters that take an `UndoManager*`).

### Schema

```
PROJECT  schemaVersion=1  name  tempo=120.0  sigNum=4  sigDen=4
         launchQuantise=4.0 (in beats; 0 = no quantize)  nextId=1  selectedTrack=<id>
├── TRACKS
│   └── TRACK  id  type="midi"   (allowed: "midi" | "audio" | "return" | "group"; this stage creates only "midi")
│             name  colour=<palette index 0..15>  volumeDb=0  pan=0  mute=0  solo=0  arm=0
│             input="all-midi"  ("all-midi" | "keyboard" | "midi:<device identifier>" | "none")
│             monitor="auto"  ("auto" | "in" | "off")
│             recordSource="midi"  ("midi" | "audio", the switch from decision no. 1; logic in 02 and 05)
│             width=120  (column width in Session View, px at 100% scale)
│      ├── DEVICES
│      │   └── DEVICE  id  kind="prisma" | "plugin" | "missing"
│      │              name  enabled=1  folded=0
│      │              pluginDesc=<PluginDescription XML, only for "plugin" and "missing">
│      │              state=<base64 getStateInformation>  (filled in on save)
│      │              macroParams="i0,i1,...,i7"  (parameter indices for the 8 knobs in the panel)
│      ├── SLOTS            (exactly as many children as there are scenes, in the same order as SCENES)
│      │   └── SLOT
│      │       └── CLIP  id  name  colour  lengthBeats=4  loopStart=0  loopEnd=4  startOffset=0
│      │           └── NOTE  start  length  pitch  velocity       (beats from the clip start)
│      └── ARRANGEMENT      (empty; filled in 03)
├── MASTER  volumeDb=0  └── DEVICES (same as a track)
├── SCENES
│   └── SCENE  id  name  colour=-1 (none)  tempo=-1 (none)
├── MARKERS                 (empty; 03)
└── SNAPSHOTS               (empty; 06)
```

Rules:
- `id` is a 64-bit integer issued by `ProjectModel::newId()` from `nextId` and never reused. Nodes refer to each other only by `id`.
- Color is stored as a **palette index**, not ARGB, so a theme can recolor everything at once. A new track gets the next color around the spectrum (0, 2, 4, … then 1, 3, 5, …) so that neighboring tracks differ.
- The number of SLOTs in each track always equals the number of SCENEs. Inserting and deleting a scene changes all tracks in a single transaction.
- Notes inside a CLIP are sorted by `start`, then by `pitch`. `ClipRef` maintains the sort order, not the calling code.

### API (minimum)

```cpp
class ProjectModel
{
public:
    juce::ValueTree root;
    juce::UndoManager undo;

    static ProjectModel createDefault();          // 2 MIDI tracks with Prisma "Init", 8 scenes
    int64_t newId();

    TrackRef addTrack (juce::String type, int insertIndex = -1);   // the caller opens the transaction
    void deleteTrack (int64_t id);
    TrackRef duplicateTrack (int64_t id);
    SceneRef insertScene (int index);
    void deleteScene (int index);
    void duplicateScene (int index);

    TrackRef findTrack (int64_t id) const;
    juce::Array<TrackRef> tracks() const;
    // ...
};
```

The UI layer opens transactions (`beginNewTransaction ("New track")`); the model only writes with `&undo`. Transaction names come from `Strings.h` and are shown in the "Undo …" menu item.

### Undo

- `Cmd+Z` / `Cmd+Shift+Z`. History limit: 500 transactions (`UndoManager (… , 500)`).
- Plugin knob changes do not go into the history in this stage (done in 06 together with automation).
- The history is cleared after a project is loaded.

## Task 1.2. Strings and settings

- `app/Source/Strings.h`: all UI text as `tr ("English", "Русский")` pairs (English is the default UI language, Russian is the second one). Track type names, menu items, status bar hints, error messages.
- `app/Source/Settings.h/.cpp`: a wrapper over `juce::PropertiesFile` with typed keys: theme, UI scale, recent projects (10), Places folders, preview volume, animations on/off, audio interface state (`AudioDeviceManager::createStateXml`), interface channel names.

## Task 1.3. Engine: lock-free playback graph

Rewrite `AudioEngine` so that:
1. There are no fixed `kNumTracks` / `kNumScenes` and no `CriticalSection`.
2. The audio thread reads only an immutable `PlaybackGraph`.

### Structures (in `app/Source/engine/`)

```cpp
// Lives as long as the track exists. Created and destroyed on the message thread;
// destruction is deferred while the audio thread may still see it (see "Deletion").
struct TrackRuntime
{
    int64_t trackId;
    std::atomic<float> volumeGain, pan;       // written by the message thread from the model
    std::atomic<bool> mute, solo, arm;
    std::atomic<float> meterL, meterR;        // written by the audio thread
    daw::engine::ClipPlayer player;           // audio thread only
    juce::AudioBuffer<float> buffer;          // allocated in prepare, size = max block
    juce::MidiBuffer midi;                    // ensureSize in prepare
    std::vector<daw::MidiEvent> events;       // reserve(4096) in prepare
};

struct DeviceInstance                          // plugin or Prisma, survives graph rebuilds
{
    int64_t deviceId;
    std::unique_ptr<juce::AudioProcessor> processor;
    std::atomic<bool> enabled;
};

struct PlaybackGraph                           // immutable after publication
{
    uint64_t generation;
    struct Track
    {
        TrackRuntime* runtime;
        std::vector<DeviceInstance*> chain;    // first is the instrument, the rest are effects
        std::vector<std::shared_ptr<const daw::engine::MidiClip>> slots;   // per scene, nullptr = empty
    };
    std::vector<Track> tracks;
    std::vector<DeviceInstance*> masterChain;
    bool anySolo;
};
```

### How the graph is published

- The message thread listens to `ProjectModel::root` (`ValueTree::Listener`) and on any structural change (tracks, scenes, clips, notes, devices) sets a "graph is stale" flag. A 60 Hz timer (or an `AsyncUpdater`) builds a new `PlaybackGraph` from the model and publishes it: `current.store (newGraph, std::memory_order_release)`.
- Clips are converted from `ValueTree` to `daw::engine::MidiClip` during the build. Unchanged clips are reused: a cache of `clipId → (content hash, shared_ptr<const MidiClip>)`.
- Volume, pan, mute, solo, and arm do not rebuild the graph: they are written into the `TrackRuntime` atomics directly from `valueTreePropertyChanged`.
- When the graph changes the clip pointer in a slot that is currently playing, `ClipPlayer::clipChanged` is called on the audio thread in the first block of the new graph (by comparing the old and new slot pointers).

### Deletion

- At the start of every block the audio thread does `graph = current.load (acquire)` and `seenGeneration.store (graph->generation, release)`.
- The message thread keeps a list of retired graphs, `TrackRuntime`s, and `DeviceInstance`s, each with the generation after which it is no longer needed. On a timer it deletes everything whose generation is ≤ `seenGeneration`. If the audio device is stopped, it deletes immediately.
- New `DeviceInstance`s get `prepareToPlay` on the message thread **before** the graph is published.

### Rest of the engine

- Transport, quantized launch, scenes, and `ClipPlayer` keep their phase 0 behavior. The "launch clip / scene / stop" commands go through an SPSC queue `EngineCommand { type, trackIndexOrId, sceneIndex }` with capacity 256.
- Position, levels, and the playing and pending slot of each track are atomics that the UI reads on a timer.
- MIDI from all enabled inputs and from the Mac keyboard goes to tracks with `arm` whose `input` matches. By default the selected MIDI track is armed automatically if no track has been armed manually.
- Track chain: the instrument receives MIDI and writes into the buffer, then the effects in order, then volume and pan (the equal-power law from phase 0), then the sum into the master, then the master chain. A disabled device is skipped (for an effect, audio passes through).
- A third-party **effect** (not an instrument) can be placed in the chain; such plugins have `numInputChannels ≥ 2`. Bus: 2 in / 2 out. Plugins with a sidechain receive only the main bus for now.
- Build flag `DAW_RT_CHECKS` (CMake option, OFF by default, ON in tests): the global `operator new` checks `thread_local bool inAudioThread` and calls `jassertfalse` plus increments a violation counter that the test reads.

## Task 1.4. Window and layout

`MainComponent` is split into components in `app/Source/ui/`:

```
┌ TopBar ─────────────────────────────────────────────────────────────────────┐
│ logo · tempo · time sig · metronome · ▶ ■ ● · position · loop · "10 min" tape│
│ · morph A↔B · [Session|Arrangement] · v14 · CPU                             │
├ Browser ┬ MainView (SessionView; ArrangementView added in 03) ──────────────┤
│         │                                                                    │
├─────────┴ DetailPanel (DeviceChainView; ClipEditor added in 02) ───────────┤
├ InfoBar: hint for the element under the mouse ──────────────────────────────┤
└──────────────────────────────────────────────────────────────────────────────┘
```

- Sizes per the concept: top bar 52 px, browser 236 px (resizable 180–420), detail panel 232 px (resizable 160–480), hint bar 22 px. Dividers are draggable, sizes are remembered in settings.
- `Cmd+Alt+B` hides the browser, `Cmd+Alt+L` hides the detail panel, `Cmd+Alt+M` hides the mixer in Session View. Triangle buttons at the edges do the same.
- The "10 min tape" and "morph A↔B" elements are drawn inactive (gray) in this stage with the hint "Coming in stage 2 / 6". The Arrangement button is inactive until 03. This is groundwork so the layout does not change later.
- **InfoBar**: every interactive element gets hint text (`setHelpText` or its own property). The bar shows the element's name and what it does. Texts come from `Strings.h`.
- UI scale 80–150% (Settings → Appearance) via `juce::Desktop::setGlobalScaleFactor`.
- The window remembers its position and size. Minimum size 1100×700.

### Session View

- Track columns from left to right, and on the right the master column with scene launch buttons. Horizontal and vertical scrolling; track headers and scene buttons stay in place while scrolling.
- The track header is in the track's color: name, selection shown with a border. A click selects it (the detail panel shows its chain). A double click or `Cmd+R` renames it. Right click: "Rename", "Color" (a grid of the theme palette's 16 colors), "Duplicate" (`Cmd+D`), "Delete" (`Delete`), "Insert MIDI Track".
- Slot states: empty (a small square; a record circle on an armed track), clip (name, color, ▶), playing (full brightness, blinking ▶, progress bar at the bottom, glow if `glow > 0`), waiting to launch (fast-blinking ▶). Colors come from `Theme`, see 1.8.
- Clips are dragged between slots with the mouse (move; with Alt, copy). `Cmd+C/Cmd+V/Cmd+D/Delete` work on the selected clip. Right click: "Rename", "Color", "Duplicate", "Delete".
- Double-clicking an empty slot creates an empty 1-bar MIDI clip (in 02 it will open in the piano roll).
- Scenes: clicking ▶ launches the scene, double-clicking the name renames it. `Cmd+I` inserts a scene after the selected one, `Cmd+Shift+I` inserts a scene with what is currently playing (scene capture), `Cmd+D` on a scene duplicates it.
- Mixer at the bottom of the columns: sends A/B (inactive until 06), pan, level meter, fader, volume in dB, "track on" buttons (the track number), Solo, Arm. Double-clicking the fader or pan resets to 0 dB / center. Cmd-clicking Solo or Arm adds to those already on; a plain click leaves only one.

### Detail panel: device chain

- `DeviceChainView` shows the selected track's devices from left to right with arrows between them and a "Drop an effect or plugin here" zone at the end.
- Device header: power switch (circle), name, preset menu (▾), fold, open in a window (↗), close (×). Devices are reordered by dragging the header; `Delete` removes the selected one. All through model transactions.
- **Prisma, compact view** (width 470 px): a mini wavetable view (like `WavetableView`, but 150×120) and 8 knobs: osc A wavetable position, osc A unison detune, filter cutoff, resonance, drive, volume envelope attack, LFO 1 rate, unison width. Knobs are bound to Prisma parameters via `AudioProcessorParameter`, not via the editor's APVTS attachments. Take the exact parameter ids from `daw::synth::parameterDescriptors()`.
- **Third-party plugin** (width 190–260 px): 8 knobs for the parameters in `macroParams`. By default, the first 8 automatable parameters. Right-clicking a knob: "Show another parameter…" with a searchable list of all parameters.
- **Missing plugin** (`kind="missing"`): a gray block "Plugin not found: SuperSynth 2 (Example Audio)". The state is kept as it was, audio passes through.
- Plugin windows and the full Prisma editor: one per device; pressing ↗ again brings the window to the front. Windows close when the device or track is deleted.

## Task 1.5. Browser

### Categories and sources

| Category | Source | What drag and drop / double click does |
|---|---|---|
| Instruments | Prisma and its preset folders; third-party instruments from `KnownPluginList` | onto a track: replace the instrument (the first device); onto an empty area or the "Drop here" zone of Session View: a new MIDI track |
| Presets | Prisma factory presets (`factoryPresets()`, in folders from the name: Bass, Lead, Pad, Pluck, FX, Init) and user presets (`.dawpreset`) | onto a track: replace the device of the same type or the instrument; onto a device: apply the preset to it |
| Effects | Only third-party effects by manufacturer for now. Built-in ones appear in 06 (a "Built-in" folder with the line "Coming in stage 6") | into the chain: add at the end or at the drop position |
| Plugins | All AU / VST3 from `KnownPluginList`, grouped by manufacturer, with a format tag | like Instruments / Effects depending on the plugin type |
| Chains | User track chains (`.dawchain`) | onto a track: replace the whole chain; onto an empty area: a new track |
| Sounds | Places folders, WAV/AIFF/FLAC/MP3 files | list only in this stage (placed on tracks in 05) |
| MIDI files | `.mid` from Places | into a slot: a clip from the file's first track with notes; onto an empty area: a new track with that clip |
| Places | User-added folders + the current project's folder | "Add Folder…" at the bottom of the list |

- Places folders are scanned in the background (`juce::ThreadPool`, one job per folder); results are sent to the message thread. Change tracking: rescan when the browser is shown if more than 10 seconds have passed.
- **Search** (`Cmd+F` focuses the field): across all categories, case-insensitive, Cyrillic and Latin. Results are grouped by category and folder and update after every letter. `Esc` clears. Up/down arrows move through the results, `Enter` puts the selected item on the selected track.
- **Preset preview**: a headphones button at the bottom of the browser turns it on. When a Prisma preset is selected, a separate Prisma instance (not on a track) plays a 2-bar phrase (a C minor arpeggio, project tempo) into the master at the volume from settings (−12 dB by default). A new phrase interrupts the old one without a click (fast release). Third-party plugins have no preview.
- **Favorites**: right-click an item → label color (7 colors). Label rows with counts appear at the top of the category list.
- **Recent**: the last 30 used items, a separate row above the categories.

### Drag and drop

- `MainComponent` is a `juce::DragAndDropContainer`. The dragged item is described by a `juce::var` holding an object `{ kind, id, path, name }` (functions `BrowserDrag::make` / `parse`).
- Targets (`DragAndDropTarget`): track header, slot, the empty area to the right of the tracks in Session View, `DeviceChainView` (highlighting the insertion point between devices), a specific device. On hover the target is highlighted with the `accent` color. An invalid target (a MIDI file onto a chain) is not highlighted.
- Dragging files from Finder: `FileDragAndDropTarget` on the same targets for `.mid` (and `.wav` etc. in 05).
- Each drop is one undo transaction ("Add Prisma · Reese Bass").

## Task 1.6. Device and chain presets

- The ▾ menu in the device header: "Save Preset…", the list of this device's presets (factory and user), "Show in Finder".
- Preset file `~/Music/Prisma DAW/User Library/Presets/<device name>/<name>.dawpreset`: XML `DAWPRESET version=1 deviceKind name pluginDesc state(base64) created`. For Prisma the device name is "Prisma", for plugins it is the plugin name.
- Right-click a track header → "Save Chain…" → `User Library/Chains/<name>.dawchain` (XML with all of the track's DEVICEs, their states, and the track color).
- Saved items appear in the browser immediately, without a restart.
- Presets can be renamed and deleted from the browser (right click). Deletion asks for confirmation in an app window.

## Task 1.7. Project file, autosave, recovery, version history

### Project folder

```
My Track Project/
├── My Track.prisma          model XML (UTF-8, indented so diffs are readable)
├── Samples/Imported/        (05) copies of dropped samples
├── Samples/Recorded/        (05) recordings
├── Backup/autosave.prisma   autosave
└── History/
    ├── index.xml            version list
    └── v0001.prisma …       the versions themselves
```

- Extension `.prisma`. The root `PROJECT` has `schemaVersion`. Loader: if the version is newer than supported, a clear error "The project was created in a newer version of the DAW"; if older, a chain of migrations `migrateFrom1To2` and so on (empty for now, but the function and a test exist).
- On save, each `DEVICE` gets `getStateInformation` written into `state` (on the message thread). Writing is atomic: a temporary file and `TemporaryFile::overwriteTargetFileWithTemporary`.
- On open: plugins are created via `AudioPluginFormatManager::createPluginInstanceAsync`; while a plugin loads, a spinner turns on the device. Not found: `kind="missing"` (1.4). Prisma is created synchronously.
- `Cmd+N` new (if there are unsaved changes, a "Save changes to “…”?" dialog with Save / Don't Save / Cancel buttons), `Cmd+O` open, "Open Recent", `Cmd+S` save, `Cmd+Shift+S` save as (copies the whole project folder). Window title: the project name and `•` if there are unsaved changes.
- A new project comes from a template: `User Library/Templates/Default.prisma` if it exists, otherwise `ProjectModel::createDefault()`. Menu item "Save as Default Template".
- On first launch the demo project from `DemoContent` opens (as in phase 0).

### Autosave and recovery

- Every 60 seconds, if there are changes since the last save or autosave, the model is written to `Backup/autosave.prisma`. A project that was never saved is autosaved to `~/Music/Prisma DAW/Unsaved/<date time> Project/`.
- On launch the app writes a marker file `~/Library/Application Support/Prisma DAW/session.lock` with the project path and deletes it on a normal exit. If the marker exists at launch and the autosave is newer than the project file, a dialog appears: "The DAW quit unexpectedly. Recover unsaved changes in “…”?" (Recover / Open Last Save / Don't Open).
- Save and autosave write files in the background: the model is serialized to a string on the message thread (this is fast), and the disk write happens on a background thread.

### Version history

- Every `Cmd+S` and every autosave after which the project changed creates a version: `History/vNNNN.prisma` and an entry in `index.xml`: `VERSION n time kind="save|autosave|restore" label=""`.
- Retention: all versions from `Cmd+S` and all labeled versions are always kept; of the autosaves, the last 50 are kept plus one per hour for older ones. Pruning runs after each new version. The pruning function lives in `core/` (pure logic over a list), with a test.
- A button in the transport shows the current version number and the time since the last save ("v14 · now", "v14 · 5 min"). Clicking it opens the version list: number, type, label, time. Double-clicking a label renames it (label "before the new bass"). The "Restore This Version" button: the current state is first saved as a new version of type `restore`, then the selected one is loaded. Nothing is lost, but undo is not available after a restore (the undo history is cleared), so the confirmation dialog says so plainly.
- "Preview Version" and "Take Track from Version" appear in 03 and 04; they are not in the list yet.

## Task 1.8. Themes and fonts

### Themes

- `app/Source/ui/Theme.h/.cpp`: a token struct and `ThemeManager` (`juce::ChangeBroadcaster`). Components take colors only from `Theme`; the root component subscribes to theme changes and calls `repaint()` on the whole tree. Themes switch on the fly, without a restart.
- Themes are JSON files in `app/Resources/themes/` (embedded via `juce_add_binary_data`): `prism.json`, `neon.json`, `studio.json`, `paper.json`. Users can put their own theme into `User Library/Themes/*.json` and it appears in the list.
- Take tokens and values from `:root[data-skin=…]` in [`docs/design/concept.html`](../design/concept.html):

| Token | Meaning | Prism | Neon | Studio | Paper |
|---|---|---|---|---|---|
| `background` | background behind the window, empty areas | #07080d | #040208 | #161718 | #e3e7ee |
| `chrome` | window panel background | #0c0e15 | #090511 | #232426 | #f6f7f9 |
| `panel` | cells, buttons, devices | #121520 | #0f0919 | #2b2c2f | #ffffff |
| `panel2` | selected and nested elements | #181c29 | #160d24 | #333438 | #eef1f5 |
| `line` | lines, borders, grid | #232838 | #2d1946 | #3e4044 | #d5dae3 |
| `text` | main text | #e6e8f2 | #f4eaff | #dcd8cf | #1b2130 |
| `dimText` | secondary text | #7f869c | #8e7aab | #8f8a82 | #667085 |
| `accent` | selection, focus, active buttons | #8be9ff | #00f0ff | #ffb547 | #2457e6 |
| `accent2` | markers, morph, secondary accent | #c59bff | #ff2bd6 | #7fc8b2 | #b5179e |
| `record` | recording, arm | #ff4d6d | #ff2b4f | #e5484d | #e5243f |
| `play` | playback, enabled devices | #6bf0a6 | #c3ff1a | #93d36c | #0f9a4c |
| `clipInk` | text on colored clips | #0b0d14 | #07030d | #1a1b1c | #141a26 |
| `clipSaturation` | palette saturation, % | 78 | 100 | 42 | 72 |
| `clipLightness` | palette lightness, % | 62 | 58 | 62 | 74 |
| `hues` | 16 palette hues, degrees | spectrum | see below | spectrum | spectrum |
| `glow` | glow strength of playing clips (0 = none) | 1.0 | 1.6 | 0 | 0 |
| `radius` | corner radius, px | 6 | 0 | 3 | 6 |
| `scanlines` | scanlines over the window | false | true | false | false |
| `fontUi` / `fontNum` | fonts | Onest / JetBrains Mono | JetBrains Mono / JetBrains Mono | Onest / JetBrains Mono | Onest / JetBrains Mono |

- Palette: 16 colors `HSL(hues[i], clipSaturation, clipLightness)`. For "spectrum", `hues = [350, 12, 24, 36, 48, 70, 110, 150, 170, 196, 214, 236, 262, 284, 306, 328]`. For Neon, shift into a magenta-cyan range: `[318, 330, 345, 290, 275, 260, 200, 186, 172, 160, 64, 52, 40, 300, 310, 335]`. An inactive clip is drawn in the same color at 55% opacity.
- Glow of a playing clip: a soft shadow in the clip color with radius `16 * glow` px (`juce::DropShadow` in a cached image, not every frame). The rainbow sheen over a playing clip (`sheen` in the concept) only if animations are enabled in settings, at 30 fps, and only for visible playing clips.
- Scanlines: a semi-transparent pattern of horizontal lines over the window, drawn once into an image and placed on top (`setInterceptsMouseClicks (false, false)`).
- Logo: a triangle in the `text` color and six rays from the palette to its right, drawn in code (`ui/Logo.h`).

### Fonts

- Embed **Onest** and **JetBrains Mono** (both under the SIL Open Font License 1.1, compatible with AGPLv3). Source: the `google/fonts` repository on GitHub (`ofl/onest/`, `ofl/jetbrainsmono/`). Put the license files alongside: `app/Resources/fonts/OFL-Onest.txt`, `OFL-JetBrainsMono.txt`.
- If JUCE cannot render different weights from a variable font file, create static 400, 500, 600 instances with `fonttools varLib.instancer` and embed those.
- `Theme::uiFont (size, weight)` and `Theme::numFont (size)`; all numbers (tempo, position, dB) use `numFont` with tabular figures.

## Task 1.9. Settings window

`Cmd+,` opens a window with tabs on the left: **Audio**, **MIDI**, **Appearance**, **Library**.

### Audio

- Driver (on macOS only Core Audio; on Linux ALSA for development), input device, output device, sample rate (44.1 / 48 / 88.2 / 96 kHz, whatever the device offers), buffer size (whatever the device offers, usually 32–2048).
- Latency, updated on every change: "Input 3.1 ms · Output 4.2 ms · Total 7.3 ms". Input = `getInputLatencyInSamples` + buffer, output = `getOutputLatencyInSamples` + buffer, converted to ms at the current sample rate.
- Channels: a list of the interface's inputs and outputs with toggles and a name field ("Microphone", "Guitar"). Names are stored in settings per device name and used in the track input menu (05).
- "Test Tone" button: 440 Hz, −20 dBFS, 1 second to the selected output.
- CPU load (`AudioDeviceManager::getCpuUsage`), updated every 0.5 s.
- "Open Audio MIDI Setup" button: launches `/System/Applications/Utilities/Audio MIDI Setup.app` (where an aggregate device is created from two interfaces). macOS only.
- An explanation under the driver list (one line): "On a Mac all audio interfaces work through Core Audio. A separate ASIO driver is not needed."
- The state is saved (`createStateXml`) and restored at launch. If the saved interface is missing, the system default is used and a message appears in the hint bar.

### MIDI

- Input list: name, a "Play on tracks" toggle, a "Control" toggle (inactive until controller support; groundwork). Devices connect and disconnect on the fly (`MidiDeviceListConnectionBroadcaster` from JUCE 8).
- Output list (display only for now).

### Appearance

- Theme selection as cards with a small preview (palette and background), UI scale, animations on/off.

### Library

- The `User Library` path (default `~/Music/Prisma DAW/User Library`), Places folders as a list with add/remove buttons, "Rescan Plugins" (opens the existing scan window).

## Task 1.10. Menus and keyboard shortcuts

- `juce::ApplicationCommandManager` with all commands, `MenuBarModel` as the main macOS menu:
  - **File:** New Project `Cmd+N`, Open `Cmd+O`, Open Recent ▸, Save `Cmd+S`, Save As `Cmd+Shift+S`, Save as Default Template, Version History…, Export… `Cmd+Shift+R` (inactive until 04), Show Project in Finder.
  - **Edit:** Undo `Cmd+Z`, Redo `Cmd+Shift+Z`, Cut `Cmd+X`, Copy `Cmd+C`, Paste `Cmd+V`, Duplicate `Cmd+D`, Delete `Delete`, Rename `Cmd+R`.
  - **Create:** MIDI Track `Cmd+T`, Scene `Cmd+I`, Scene with Playing Clips `Cmd+Shift+I`, Empty MIDI Clip `Cmd+Shift+M`.
  - **View:** Browser `Cmd+Alt+B`, Detail Panel `Cmd+Alt+L`, Mixer `Cmd+Alt+M`, Session / Arrangement `Tab` (inactive until 03), Zoom +/− `Cmd+=` / `Cmd+-`.
  - **Settings…** `Cmd+,` (in the macOS application menu).
- Space always plays/stops, except when focus is in a text field. `M` toggles playing on the Mac keyboard (indicator in the transport); when on, letters play notes, as in phase 0.
- Keyboard shortcuts are not intercepted while focus is in a text field (renaming, search).

## Tests

In `tests/` (JUCE) and `core/tests/`:

1. **Model.** Create a project, add 3 tracks, 2 scenes, a clip with 10 notes, rename, delete a track. Each operation in its own transaction. Undoing all the way gives XML equal to the original; redoing all the way gives XML equal to the final state.
2. **Scene invariant.** After inserting and deleting scenes, every track has as many SLOTs as there are SCENEs; clips stay in their scenes.
3. **Save and load.** A project with a Prisma whose filter cutoff was changed is saved to a temporary folder and loaded: the XML matches (excluding `state`), the Prisma parameter equals the saved value, and the audio of the first second of the demo clip matches the original sample for sample (rendered via `FakeDevice`).
4. **Missing plugin.** A `DEVICE kind="plugin"` with a nonexistent `pluginDesc` loads as `missing`, and its state is not lost when the project is saved again.
5. **Schema version.** A file with `schemaVersion=999` produces an error, not a crash.
6. **Lock-free graph.** With `DAW_RT_CHECKS=ON` the engine runs 2000 blocks of 256 samples while a parallel thread (simulating the message thread) adds and removes tracks, clips, and devices. The audio thread allocation counter is 0, and there are no NaN or Inf values in the output.
7. **Clip continuity.** A playing clip does not restart and does not lose notes when another track is added: the number of note-ons over 4 bars is the same as without a rebuild.
8. **Deletion.** After 100 graph rebuilds, no more than 3 `PlaybackGraph`s are alive.
9. **Version pruning** (core): a set of 300 autosaves over 5 days + 5 saves + 2 labels yields the expected list.
10. **Recovery.** Session marker present and autosave newer than the project → `RecoveryCheck::needsRecovery` returns true; false in all other cases.
11. **Browser search.** "reese", "REESE", "Рее" (if the name is in Cyrillic) find the right items; an empty string returns everything.
12. **Themes.** All 4 JSON files parse, all tokens are present, the palette has 16 colors.

## Done when

- [ ] The window looks like the concept in Session View in all four themes (4 screenshots in the report and in `media/`).
- [ ] You can create 20 tracks and 16 scenes, scrolling works, headers and scene buttons stay in place.
- [ ] A Prisma preset from the browser can be dragged onto an empty area (new track), onto a track (instrument replaced), and onto a Prisma device (preset applied).
- [ ] A third-party effect from the browser goes into the chain and is audible; the device power switch passes audio through.
- [ ] A custom Prisma preset and a custom chain are saved and immediately visible in the browser.
- [ ] The project saves, closes, and reopens with the same sound from all plugins (test 3 and a manual check on a Mac).
- [ ] `Cmd+Z` undoes each action from tasks 1.4–1.6 one at a time.
- [ ] A forced quit (`kill -9`) after changes and a relaunch offer recovery, and recovery brings the changes back.
- [ ] The version list shows saves and autosaves, a label can be set, restoring a version works and saves the current state as a new version.
- [ ] The audio settings show latency and change the buffer without a restart; the test tone plays.
- [ ] All tests from the list are written and green, and so are the old ones. CI is green, the DMG builds.
- [ ] README updated: new features, keyboard shortcuts, screenshots.
