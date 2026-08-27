# AGENTS.md — QWavRec

Guidance for humans and coding agents working on this codebase.

## What this project is

**QWavRec** is a small, deliberately boring Qt Widgets desktop app for:

- Recording WAV from a PulseAudio **source** (including **monitor** sources)
- Playing WAV to a PulseAudio **sink**
- Browsing cached takes, simple normalize, A–B loop region on the waveform

It is **not** a DAW, mixer, patchbay, or effects rack. If a feature belongs in
`pavucontrol`, Audacity, or Ardour, it does not belong here.

License: **GPL-3.0-or-later** (REUSE / SPDX headers on sources).

## Build & run

- **Build system:** CMake + **Nix flake** (`nix run .` / `nix develop`)
- **Qt:** Qt 6 (Widgets + Multimedia only for `QAudioFormat`)
- **Audio:** **libpulse** + **libpulse-simple** (not Qt Multimedia I/O)
- Author on commits: `Ingo Ruhnke <grumbel@gmail.com>` with
  `Co-authored-by: Grok <grok@x.ai>` when applicable

```bash
nix run .          # build & run
nix develop        # shell with cmake/qt
```

Delivery of changes in this workflow uses **stacking git bundles**
(`qwavrec-NNN-*.bundle`), continuously numbered, `HEAD` as ref.

## Architecture (mental model)

```
┌─────────────────────────────────────────────┐
│ MainWindow (Qt Widgets, GUI thread only)    │
│  - menus/toolbar, device combos, meters     │
│  - QTimer ~20 Hz → onMeterTick()            │
└───────────────┬─────────────────────────────┘
                │
     ┌──────────┼──────────┐
     ▼          ▼          ▼
PulseCapture  PulsePlayback  PulseDevices
 (worker)      (worker)       (sync, rare)
     │              │
  pa_simple      pa_simple
  RECORD         PLAYBACK
     │              │
     └──── PulseAudio / PipeWire (via pulse) ──┘
```

- **GUI thread must never block** on `pa_simple_read/write` or on long
  `QThread::wait` without processing events carefully.
- **Capture** runs continuously while monitoring (input meter). Recording
  only flips a flag so PCM is copied into a queue the GUI drains.

## PulseAudio API lessons (critical)

### Prefer libpulse for devices; Qt Multimedia hides monitors

Qt `QMediaDevices::audioInputs()` **does not list monitor sources**
(`monitor_of_sink`). Listing monitors requires **libpulse**
`pa_context_get_source_info_list` and checking
`i->monitor_of_sink != PA_INVALID_INDEX`.

### Never open many synchronous Pulse connections on the GUI thread

Early code called default-source, list-sources, default-sink, list-sinks as
**four separate connect/iterate cycles** → multi-second freezes.

**Correct pattern:** `PulseDevices::query()` — **one** `pa_context` + mainloop:

1. `pa_context_get_server_info` → default source/sink names  
2. `pa_context_get_source_info_list` → sources (tag monitors)  
3. `pa_context_get_sink_info_list` → sinks  

Cap iterate loops so a hung server cannot freeze the app forever.

### `pa_simple` is blocking — design around that

| Call | Behavior |
|------|----------|
| `pa_simple_read` | Blocks until a fragment is available |
| `pa_simple_write` | Blocks when the buffer is full |
| `pa_simple_drain` | Blocks until playback drained |

**Do not** `QThread::wait()` on a worker stuck in `pa_simple_read` from the
GUI thread without a short timeout + event processing — and prefer **not
waiting at all** for capture stop.

Use `pa_buffer_attr` with ~20 ms `fragsize` / `tlength` so reads/writes
return more often and stop latency stays bounded.

### Capture design (current)

- Worker thread: `pa_simple` RECORD loop  
- **Peak:** `std::atomic<qreal>` updated every fragment (lock-free)  
- **Recording PCM:** appended under mutex when `m_recording` is true  
- GUI: `QTimer` 50 ms → `currentPeak()` for meter, `takeRecordedAudio()` while recording  
- **Do not** emit `samplesReady` with a `QByteArray` 50×/s — floods the event queue and makes the UI choppy  

### Stop recording without freezing

**Wrong:**

```cpp
capture->stop();                 // invalidates session / drops buffers
for (...) { processEvents(); QThread::msleep(10); }  // freezes GUI, re-entrancy
```

**Right:**

```cpp
// finishRecordingStop():
capture->setRecording(false);
// drain queue into m_recordPcm + writer (no sleep)
wavWriter.close();
// peaks from m_liveRecordPeaks; player->loadPcm(m_recordPcm) — no disk read
setAppState(Ready);              // unlock UI first
QTimer::singleShot(0, archiveTake);  // QFile::copy off the critical path
```

Never call `loadDocumentForPlayback` + `QFile::copy` synchronously on the
record-stop path — that was the multi-second freeze after a take.

- Leave the capture stream running for the input meter after stop.  
- **Never `QThread::msleep` on the GUI thread.**  
- Avoid `processEvents` during stop — it re-enters the meter timer and can
  deadlock or re-enter `onRecord`.

### Session / generation counters

Bumping a session id is used to ignore late work from superseded workers.
**Do not bump session on every soft stop** if you still need in-flight PCM
for the current take. Bump when **starting** a new capture stream.

### Playback

- Load full PCM into memory (`WavFile::load` + `PulsePlayback::loadPcm`).  
- Worker writes with `pa_simple_write`; throttle `positionChanged` (~15 Hz).  
- A–B region: `setPlayRange(startMs, endMs)`; loop stays inside the range.  
- `stop()` may wait briefly with `processEvents(ExcludeUserInputEvents)`.

## WAV I/O

- **`WavWriter`:** placeholder 44-byte header on open; **finalize sizes in
  `close()`** (`seek(0)`, rewrite RIFF/data sizes, `flush`).  
- **`WavFile::load`:** manual chunk parser (`fmt ` / `data`). Accept empty
  data chunk; do not treat `dataSize == 0` as “missing chunk”.  
- Record path is Int16 mono 48 kHz by default.  
- Empty recording after a race used to surface as
  `"WAV missing fmt or data chunk"` — usually “no PCM was written”, not a
  corrupt header.

## Document / cache model

- Takes are archived under **`$XDG_CACHE_HOME/qwavrec/`** (`rec-*.wav`).  
- History supports previous/next and a History dialog; prune to max 50.  
- No quit prompt to “save” — cache is the safety net; **Save/Save As**
  export explicitly.  
- Window title: file name, or `Take N/M (cache)`, or `Unexported take`.

## UI conventions

- Standard theme icons with `QStyle::StandardPixmap` fallbacks.  
- Transport: Record / Play (toggle pause) / Stop; loop; normalize.  
- Waveform: static peaks for document; live peaks while recording;
  **drag = A–B selection**, click = seek, double-click = clear selection.  
- Mic gain slider 0–300% (default 100%); marker at 100%.  
- Input/output device and volumes persisted via `QSettings`.

## Qt 6 pitfalls already hit

| Issue | Fix |
|-------|-----|
| `Qt::DefaultLocaleShortDate` removed | `QLocale::system().dateTimeFormat(QLocale::ShortFormat)` |
| `Q_DECLARE_METATYPE` redefinition via moc | Prefer `Q_ENUM(State)` on `QObject` |
| Slot declared, never defined | Link error from moc — remove or implement |
| `QFile::open` nodiscard | Check return value |

## Non-goals (do not grow into)

Audacity, Ardour, DAW, music library, mixer, PipeWire patchbay, effects
rack, analyzer, conversion suite. Prefer shelling out or pointing users at
existing PipeWire/Pulse tools when appropriate.

## Files to know

| Path | Role |
|------|------|
| `src/mainwindow.*` | UI, state machine, timer, document ops |
| `src/pulsebackend.*` | Pulse enumerate / capture / playback |
| `src/wavfile.*` | Load + peaks + peak-normalize Int16 |
| `src/wavwriter.*` | Stream write + header finalize |
| `src/waveformwidget.*` | Peaks, playhead, A–B selection |
| `src/levelmeter.*` | Input/output level widgets |
| `src/recordinghistory.*` | Cache takes under XDG |
| `src/historydialog.*` | Take browser dialog |
| `flake.nix` / `CMakeLists.txt` | Nix + Qt + libpulse |

## Working rules for agents

1. Plan before large edits; prefer small commits.  
2. **Never block the GUI thread** on Pulse I/O or long sleeps.  
3. Device list = **one** Pulse connection.  
4. Meter/PCM path = atomics + timer, not 50 Hz signal storms.  
5. Monitors are a **hard requirement** — keep libpulse enumeration.  
6. Stack git bundles cleanly if that delivery process is in use.  
7. Keep the app small; push complex audio work to other tools.

## Known remaining limitations

- `pa_simple` cannot cancel a blocked read; stop is cooperative after the
  current fragment (~20 ms with our `fragsize`).  
- Full-file PCM in memory limits practical take length.  
- No hotplug subscription yet (manual refresh / restart).  
- Device list still runs synchronously on the GUI thread (one connection,
  but can hitch briefly on a slow server) — a future improvement is a
  worker + cached list.  
- Select/cut of regions is still a TODO beyond A–B play/loop.

### Residual record-stop hitches (post-83b8764 investigation)

Catastrophic freezes (disk `WavFile::load`, synchronous `QFile::copy`,
blocking capture wait) are fixed. Remaining GUI-thread work that can still
hitch on stop of a long take:

1. **`PulsePlayback::loadPcm` full `QByteArray` copy** after
   `setAppState(Ready)` but before the slot returns — O(n) memcpy of the
   whole take (≈ 29 MB for 5 min mono 48 kHz Int16).
2. **Deferred archive still on the GUI thread** — `QTimer::singleShot(0)`
   runs `archiveTake` on the next event-loop turn. Rename is instant; the
   `QFile::copy` fallback (cross-device temp vs cache) blocks the UI again.
3. **Peak memory ≈ 3× take size** (temp file + `m_recordPcm` + player
   `m_pcm`) until the singleShot clears `m_recordPcm` — can induce
   swapping on low-RAM systems and amplify every subsequent hitch.
4. **`WavWriter::close()` → `flush()`** still runs before Ready; usually
   cheap on tmpfs, not always.
5. **`m_recordPcm` is never reserved** — repeated `append` during the take
   causes geometric realloc copies (jank while recording, possible final
   realloc on drain).

Also: `onStop` while Playing/Paused currently stops the capture stream
(brace/indentation accident); that contradicts “leave capture running for
the meter” and forces a monitoring restart. Capture `stop()` is
non-blocking, so this is not a multi-second freeze.

Preferred directions (no new `processEvents` / sleep hacks):

- Move or ownership-transfer the PCM into the player (avoid the copy), or
  load from the archived path lazily on first Play.
- Keep rename on the GUI thread; if copy is required, do it on a worker
  and update UI via queued signals.
- Align temp and cache on the same filesystem so rename almost always wins.
- `reserve()` a reasonable capacity when a take starts.
- Do not stop capture when only stopping playback.
