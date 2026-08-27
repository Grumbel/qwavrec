# QWavRec TODO

## Done

- [x] Qt Widgets main window
- [x] Input / Output device dropdowns with hot-plug
- [x] File Open / Save Recording As (separate actions)
- [x] Play / Record / Stop with proper icons (red record button)
- [x] Big square-ish transport buttons at the bottom
- [x] Menu bar and toolbar
- [x] Seek slider and time display
- [x] WAV recording
- [x] Basic error reporting
- [x] Application icon (SVG, old-school colorful)
- [x] Realtime input level meter (LED-style)
- [x] Output level meter during playback
- [x] Scrolling waveform / signal display (phosphor-style)

## Feature requests / Nice to have

- [ ] Pause button (in addition to Resume via Play)
- [ ] Volume control slider
- [ ] Full static waveform of the loaded file (decode peaks with QAudioDecoder)
- [ ] Peak-hold and clip indicators on meters
- [ ] Remember last used devices and directories
- [ ] Default recording directory preference
- [ ] More recording formats (FLAC, Opus, …) when the backend supports them
- [ ] Auto-restart stream on device change instead of stopping
- [ ] Keyboard shortcuts for transport (Space = play/pause, R = record, …)
- [ ] Man page
- [ ] Translations / i18n
- [ ] Optional always-on-top or tray mode
- [ ] Simple spectrum (FFT) view as alternative to waveform
- [ ] Drag-and-drop of audio files onto the window

## Non-goals

- Waveform editor / destructive editing
- Effects / filters / EQ
- Playlist or media library
- Mixer / PipeWire patchbay
- Multi-track recording
- Metadata tagging UI
- Podcast / chapter tools
