# QWavRec TODO

## Audit (2026-08-27)

Findings from a full pass over the UI, document model, and source.
**Do not treat this as a feature wishlist alone** — items below are
ordered roughly by severity / user impact. Fixes are intentionally
deferred to follow-up commits.

---

### Residual GUI hitch on record-stop (investigated)

See AGENTS.md “Residual record-stop hitches (post-83b8764 investigation)”.

- [ ] Avoid full-take `QByteArray` copy in `loadPcm` on the GUI thread
      (move/swap ownership, or lazy load from archive on first Play).
- [ ] Archive: keep rename on GUI thread; run `QFile::copy` fallback on a
      worker if rename fails (cross-device). Prefer same filesystem for
      temp and `$XDG_CACHE_HOME/qwavrec`.
- [ ] `m_recordPcm.reserve()` when a take starts; drop dual full buffers
      if possible to cut peak memory.
- [ ] Fix `onStop` so stopping playback does not stop the capture stream.

### Monitors (PulseAudio / PipeWire)

**Resolved in pulse backend:** sources are listed via libpulse,
including monitors (`monitor_of_sink`). Input combo shows a
`[monitor]` tag.

~~Qt Multimedia (since ~6.4) **deliberately filters out** PulseAudio
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
combo. About text already mentions this; a short note is shown under the Input dropdown; monitors still require remap or native API.

---

### UI / UX consistency (HIG-ish)

Compared to typical GNOME/KDE/Qt desktop guidelines and common media apps:

- [x] **Document model is hard to explain** (title: Take N/M (cache) / Unexported take / file name)  
  “Untitled / Take N/M / cache path / modified*” mixes three concepts:
  ephemeral take, durable cache archive, and user-saved file. Window
  title alone is not enough for “where is my file?”. Prefer a clearer
  model: *current buffer* vs *last exported path*, and surface the
  cache only under a “History / Takes” UI.

- [x] **Undo/Redo label vs behaviour** (→ Previous/Next Take, Transport menu, Ctrl+Left/Right)  
  File → Undo Take is not document undo (text/waveform edit). It
  navigates a take list. That conflicts with standard Edit → Undo
  expectations and shortcuts (Ctrl+Z). Rename to “Previous take” /
  “Next take” and move under Transport or a Takes menu; free Ctrl+Z
  for real edit undo later.

- [x] **Record is checkable; Play is checkable; Stop is not**  
  Play is checked only while *Playing* (pause icon); unchecked when
  Paused/Stopped (play icon). Record stays available while paused or
  playing (starts a new take after stopping playback). Stop ends
  play/record and rewinds.

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

- [x] **Desktop entry** (comment no longer claims PipeWire-only)  
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
  is replace-current + archive previous. **Decided for now:** whole-take
  Record + History is the model; tape-style insert is a separate,
  optional splice→new-take idea (see “Tape-style insert” below) — not
  silent append.

- [x] **`m_modified` after archive** (quit prompt clarifies cache vs export)  
  Cache files are durable, but we still mark modified and prompt on
  quit. Reasonable for “not exported to Music/”, but the prompt says
  “has not been saved” which is ambiguous if the take is already in
  `~/.cache/qwavrec`.

- [x] **Normalize only supports Int16** (warns the user on failure)  
  Float/other formats fail silently-ish (`return false`). Should
  message the user.

- [ ] **Capture format lock**  
  We prefer 48 kHz mono Int16; if the device cannot do that, preferred
  format may be stereo/float and WAV headers follow that — OK, but
  channel count >1 is not reflected in the UI.

- [x] **Input monitor vs exclusive device** (capture released while playing; restarted when idle)  
  Capture stream stays open for metering while idle. Some devices /
  Bluetooth profiles dislike always-on capture; may block other apps
  or fail to start record. Consider starting the source only when
  needed (record or explicit “monitor” mode).

- [x] **Output meter is peak-from-file, not post-volume** (scaled by playback volume)  
  Level is independent of the playback volume slider (pre-fader).
  Either document that or apply volume in the meter.

- [ ] **Loop + seek edge cases**  
  Recently reworked; still worth stress-testing: seek near EOF with
  loop on, pause during loop restart, change output device while
  playing.

- [x] **History grows without bound** (pruned to 50 takes)  
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

- [x] **Extract peak analysis** (`WavFile` helpers for load/peaks/normalize)  
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

- [x] Select and cut sections of the recording — cut/copy/paste/delete/crop
  with edit undo; A–B edge drag; Record Insert mode.
- [x] Insert recording at playhead — Transport → Record Insert (checkable).
  Splices capture into the current document; normal Record still makes a new take.
- [ ] True in-RAM buffer without temp files (cache already helps)
- [ ] Peak-hold / clip LED on meters
- [ ] Drag-and-drop files onto the window
- [ ] Man page / translations
- [ ] REUSE is present; verify `reuse lint` clean on release

---

### Tape-style insert / second Record (design note — do not implement casually)

**Request:** a second control that records *into* the current document at
the playhead (old tape punch-in / insert), instead of always starting a
brand-new take. Attractive for ad-hoc voice notes; dangerous scope creep
toward a full WAV editor (cut, copy, paste, undo stack, multi-format).

**Tension with current UI**

| Concept today | Tape insert needs |
|---------------|-------------------|
| Record = new take; old one archived in History | Mutate or fork the *current* PCM at a time offset |
| Takes are whole files in `$XDG_CACHE_HOME/qwavrec` | Either rewrite one file or produce a new take that is a splice |
| A–B selection only limits play/loop | Selection might mean “replace this range” vs “insert at point” |
| No clipboard, no undo beyond Previous/Next take | Cut/copy/paste implies document editing |

**If we ever want a minimal version (still dead simple)**

Keep History as the only “undo”. No general editor:

1. **One optional action:** e.g. “Record from here” / “Insert record”
   (not a permanent second big red button unless it stays obvious).
2. **Semantics:** at Stop of an insert session, build
   `left = PCM[0..playhead)`, `right = PCM[playhead..end)` (or drop
   right if *overwrite to end*), `out = left + newCapture [+ right]`.
3. **Always archive the result as a *new* take**; leave the previous
   take untouched in History (Previous take recovers the pre-insert
   version). Never silently destroy the only copy.
4. **Require matching format** (already 48 kHz mono Int16 on the record
   path) — no resampling UI.
5. **No** cut/copy/paste, no multi-track, no effects beyond existing
   normalize. “Delete selection” could be a later one-liner on the same
   splice helper (`left + right`) if insert proves useful — still not a
   suite.

**UI sketch (only if building the minimal version)**

- Prefer a **modifier on Record** (e.g. Shift+Record, or a small
  “Insert at playhead” in Transport menu) over a second large Record
  button — two red buttons fight the “dead simple” layout.
- Status line while armed: `Inserting at 1:23…` so it is obvious this
  is not a fresh take.
- If there is an A–B selection: either disable insert or define one
  rule only — e.g. **replace selection** with the new capture (still
  one new take). Do not offer both insert and replace without a clear
  single default.

**Recommendation**

Do **not** add this until the basic transport/record-stop paths stay
boring and reliable. Prefer external editors (Audacity, etc.) for real
surgery. If product pressure returns, implement only the splice→new-take
path above; treat full edit as a **non-goal**.

---

### Non-goals (unchanged)

- DAW / multi-track / effects beyond normalize
- **Full WAV editor** (clipboard, multi-undo, effects chain, multi-format)
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
