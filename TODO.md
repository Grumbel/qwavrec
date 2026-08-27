# QWavRec TODO

## Done

- [x] Qt Widgets UI with menu + toolbar
- [x] Device selectors with hot-plug; settings persisted across sessions
- [x] Document model + XDG cache history with Undo/Redo
- [x] Record / Play / Pause / Stop; loop toggle
- [x] Mic gain 0–300% with unity marker; playback volume
- [x] Live normalized waveform while recording; static peaks after
- [x] Peak Normalize action (no clipping)
- [x] Low-level WAV capture (QAudioSource) and playback (QAudioSink)
- [x] Theme icons with QStyle fallbacks

## Feature requests

- [ ] Select and cut sections of the recording
- [ ] Append successive recordings into one document
- [ ] True in-RAM sample buffer (instead of temp/cache files)
- [ ] Peak-hold / clip indicators on meters
- [ ] Open non-WAV formats (decode to WAV for editing)
- [ ] PulseAudio monitor sources (needs libpulse; Qt hides them)
- [ ] Drag-and-drop of audio files
- [ ] Man page / translations

## Non-goals

- Waveform editor / multi-track DAW features
- Effects / EQ / filters beyond normalize
- Playlist / media library
- PipeWire patchbay / mixer
