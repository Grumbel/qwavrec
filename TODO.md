# QWavRec TODO

## Audit (2026-08-27)

Findings from a full pass over the UI, document model, and source.
**Do not treat this as a feature wishlist alone** — items below are
ordered roughly by severity / user impact. Fixes are intentionally
deferred to follow-up commits.

---

### Monitors (PulseAudio / PipeWire)

**Still not listed — this is expected with the current stack.**

Qt Multimedia (since ~6.4) **deliberately filters out** PulseAudio
sources where `monitor_of_sink != PA_INVALID_INDEX`. The same idea
applies on the PipeWire backend: only “real” capture sources
(microphones, etc.) appear in `QMediaDevices::audioInputs()`.

There is **no Qt API switch** to show monitors. Options if we want them:

1. **libpulse** (or PipeWire’s SPA/native API) to enumerate sources,
   including monitors, then open capture via `QAudioSource` using a
   device id if the Qt backend can resolve it — often it cannot for
   monitors.
2. **Open the monitor by Pulse/PipeWire name** outside Qt (custom
   `pw_stream` / `pa_simple`) — real work, new dependency.
3. **Document the workaround**:  
   `pactl load-module module-remap-source master=SINK.monitor source_name=…`  
   so a monitor shows up as a normal source in Qt.

Until (1) or (2) is done, **monitors will not appear** in the Input
combo. About text already mentions this; the UI could be clearer
(e.g. a short note under the Input dropdown).

---

### UI / UX consistency (HIG-ish)

Compared to typical GNOME/KDE/Qt desktop guidelines and common media apps:

- [ ] **Document model is hard to explain**  
  “Untitled / Take N/M / cache path / modified*” mixes three concepts:
  ephemeral take, durable cache archive, and user-saved file. Window
  title alone is not enough for “where is my file?”. Prefer a clearer
  model: *current buffer* vs *last exported path*, and surface the
  cache only under a “History / Takes” UI.

- [ ] **Undo/Redo label vs behaviour**  
  File → Undo Take is not document undo (text/waveform edit). It
  navigates a take list. That conflicts with standard Edit → Undo
  expectations and shortcuts (Ctrl+Z). Rename to “Previous take” /
  “Next take” and move under Transport or a Takes menu; free Ctrl+Z
  for real edit undo later.

- [ ] **Record is checkable; Play is checkable; Stop is not**  
  Mixed toggle semantics. Many recorders use momentary Record that
  latches, or exclusive transport states (radio-like). Align icons
  and checked state with a single transport state machine.

- [ ] **Large transport buttons vs toolbar**  
  Record/Play/Stop only at the bottom; toolbar has New/Open/Save and
  Loop/Normalize. Fine, but Loop on the toolbar while Stop is only
  bottom-row is asymmetric. Either put a compact transport group on
  the toolbar or keep all transport only in one place.

- [ ] **Seek bar while stopped**  
  Enabled when a document exists; good. Click-to-seek on the groove
  should be verified (style-dependent). No time tooltip on hover.
  No page-step aligned to e.g. 1s/5s.

- [ ] **Destructive actions without clarity**  
  Starting Record replaces the current unsaved view (new temp) while
  previous takes remain in cache — easy to think data was lost.
  Prompt or status: “Previous take kept in history”.

- [ ] **Accessibility**  
  No `setAccessibleName` / `setAccessibleDescription` on meters,
  waveform, or custom record icon. Level meters are purely visual.
  Keyboard: Space/R/Esc exist; focus order through combos/sliders
  not audited. High-contrast / color-blind: red record + green wave
  only.

- [ ] **i18n**  
  All UI strings use `tr()`, good. No `.ts` files or lupdate in the
  build. Plural forms for “Take %1/%2” ok; cache messages not reviewed
  for translators.

- [ ] **Desktop entry**  
  Comment still says “PipeWire” though capture is Qt Multimedia
  (Pulse/PipeWire backend). Categories look fine. No
  `StartupWMClass` if the WM class differs from the binary name.

- [ ] **Status bar vs dialogs**  
  Errors use modal `QMessageBox`; transient info uses status bar.
  OK in principle; long errors in status bar would be wrong (none
  currently). “Ready” overwrites useful “Take saved to cache” after
  state changes — easy to miss.

---

### Behavioural bugs / sharp edges

- [ ] **Recording starts a brand-new take** and does not append (original
  design brainstorm asked for append until File→New). Current behaviour
  is replace-current + archive previous. Decide and document one model.

- [ ] **`m_modified` after archive**  
  Cache files are durable, but we still mark modified and prompt on
  quit. Reasonable for “not exported to Music/”, but the prompt says
  “has not been saved” which is ambiguous if the take is already in
  `~/.cache/qwavrec`.

- [ ] **Normalize only supports Int16**  
  Float/other formats fail silently-ish (`return false`). Should
  message the user.

- [ ] **Capture format lock**  
  We prefer 48 kHz mono Int16; if the device cannot do that, preferred
  format may be stereo/float and WAV headers follow that — OK, but
  channel count >1 is not reflected in the UI.

- [ ] **Input monitor vs exclusive device**  
  Capture stream stays open for metering while idle. Some devices /
  Bluetooth profiles dislike always-on capture; may block other apps
  or fail to start record. Consider starting the source only when
  needed (record or explicit “monitor” mode).

- [ ] **Output meter is peak-from-file, not post-volume**  
  Level is independent of the playback volume slider (pre-fader).
  Either document that or apply volume in the meter.

- [ ] **Loop + seek edge cases**  
  Recently reworked; still worth stress-testing: seek near EOF with
  loop on, pause during loop restart, change output device while
  playing.

- [ ] **History grows without bound**  
  Every take is kept under `~/.cache/qwavrec` with no max count/size
  or UI to prune. Risk of filling disk on long sessions.

- [ ] **Open only WAV**  
  Dialog filters to WAV; non-WAV “All files” will fail in WavPlayer
  with an error dialog — OK, but the filter suggests we only support
  WAV (true for now).

- [ ] **Concurrent record + device change**  
  Changing input while recording stops recording via `onRecord()` —
  side effect of combo change. Surprising.

---

### Architecture / refactoring

`mainwindow.cpp` is ~1200 lines and owns UI, settings, capture,
document paths, history, normalize, and WAV peak extraction.

- [ ] **Extract `Document` / `Session` class**  
  Paths (`m_tempPath`, `m_savedPath`, `m_isTemporary`, `m_modified`),
  load/save/maybeSave, and history pointer belong together — not in
  the widget.

- [ ] **Extract peak analysis**  
  `setWaveformFromPcm` and normalize’s WAV walk duplicate header
  parsing already done in `WavPlayer` / `WavWriter`. One
  `WavFile::{read,write,peaks,normalize}` helper.

- [ ] **Transport controller**  
  Explicit state machine (Idle / Recording / Playing / Paused) with
  one place that enables actions and owns transitions. Today
  `AppState` plus scattered checks in every slot.

- [ ] **Settings**  
  Load/save is fine; keys are magic strings. Centralize in a small
  `Settings` struct or namespace.

- [ ] **Tests**  
  No unit tests for WavWriter header sizes, WavPlayer position math,
  or normalize gain. Pure logic is easy to test without audio hardware.

---

### Missing vs original product intent

- [ ] Select and cut sections of the recording (user request)
- [ ] Append recordings until File→New (brainstorm; may reject)
- [ ] True in-RAM buffer without temp files (cache already helps)
- [ ] Peak-hold / clip LED on meters
- [ ] Drag-and-drop files onto the window
- [ ] Man page / translations
- [ ] REUSE is present; verify `reuse lint` clean on release

---

### Non-goals (unchanged)

- DAW / multi-track / effects beyond normalize
- Playlist / library
- PipeWire patchbay / mixer UI
- Competing with Audacity or pw-record CLI power users

---

## Done (summary)

- Qt Widgets UI, menus, toolbar, large transport buttons
- Device lists + hot-plug; settings persistence
- QAudioSource capture + WavWriter; QAudioSink WavPlayer
- Cache history with previous/next take
- Mic gain 0–300% + unity marker; playback volume
- Waveform (optional auto-scale); normalize action
- Loop; Stop; seek when document loaded
