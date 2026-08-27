# QWavRec TODO

## Done

- [x] Qt Widgets main window with menu + toolbar
- [x] Input / Output device dropdowns with hot-plug
- [x] Document model: record into temporary file until explicit Save
- [x] File → New / Open / Save / Save As
- [x] Record and Play as toggles
- [x] Icon-only large transport buttons
- [x] Status bar only
- [x] Playback volume + mic gain (0..200%, default 100%)
- [x] Recording via QAudioSource + WAV writer (reliable length, live peaks)
- [x] Live waveform while recording; static document waveform with playhead after
- [x] Seek slider and time display
- [x] Application icon (old-school SVG)

## Known limitations

- [ ] PulseAudio/PipeWire *monitor* sources are filtered out by Qt Multimedia
      (since Qt 6.4). Listing them requires libpulse / PipeWire API, not planned
      for the thin Qt-only client. Workaround: `pactl load-module module-remap-source`.

## Feature requests / Nice to have

- [ ] True in-RAM sample buffer (instead of temp file)
- [ ] Append successive recordings into one document
- [ ] Peak-hold / clip indicators on meters
- [ ] Remember last devices, volumes and directories
- [ ] More recording formats
- [ ] Keyboard shortcuts (Space = play/pause, R = record)
- [ ] Drag-and-drop of audio files
- [ ] Man page / translations

## Non-goals

- Waveform editor / destructive editing
- Effects / filters / EQ
- Playlist or media library
- Mixer / PipeWire patchbay
- Multi-track recording
- Metadata tagging UI
