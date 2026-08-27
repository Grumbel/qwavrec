# QWavRec TODO

## Done

- [x] Qt Widgets main window with menu + toolbar
- [x] Input / Output device dropdowns with hot-plug
- [x] Document model: record into temporary file until explicit Save
- [x] File → New / Open / Save / Save As
- [x] Record and Play as toggles (no separate Stop)
- [x] Icon-only large transport buttons (tooltips only)
- [x] Status bar only (no duplicate status label)
- [x] Playback volume + microphone level sliders
- [x] Seek slider and time display
- [x] WAV recording
- [x] Realtime input level meter + output meter
- [x] Scrolling waveform display
- [x] Application icon (old-school SVG)
- [x] Avoid concurrent QAudioSource while recording (prevents truncated end)

## Feature requests / Nice to have

- [ ] True in-RAM sample buffer (instead of temp file) for the document
- [ ] Append successive recordings into one document
- [ ] Full static waveform of the loaded/saved file (QAudioDecoder peaks)
- [ ] Peak-hold / clip indicators on meters
- [ ] Remember last devices, volumes and directories
- [ ] More recording formats when the backend supports them
- [ ] Auto-restart stream on device change
- [ ] Keyboard shortcuts (Space = play/pause, R = record, …)
- [ ] Drag-and-drop of audio files
- [ ] Man page / translations

## Non-goals

- Waveform editor / destructive editing
- Effects / filters / EQ
- Playlist or media library
- Mixer / PipeWire patchbay
- Multi-track recording
- Metadata tagging UI
