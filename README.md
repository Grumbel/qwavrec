<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# QWavRec

A small, traditional Qt desktop application for playing and recording audio via PipeWire (through Qt Multimedia).

Essentially a GUI companion to `pw-play` / `pw-record`:

* Select audio input or output device
* Record audio to a WAV file
* Play an audio file
* Switch devices without restarting the application
* See realtime input activity and a simple waveform
* Quickly verify that a microphone or speaker works

## Features

* Input / Output device selectors with hot-plug support
* Separate **Open** (playback) and **Save Recording As** actions
* Record / Play / Stop with proper icons (red record button)
* Large transport buttons, menu bar and toolbar
* Seek slider and time display
* Realtime input level meter (LED segments)
* Output level meter and scrolling waveform during playback/monitoring
* Old-school colorful SVG application icon

## Building

### With Nix

```bash
nix build
./result/bin/qwavrec
```

Or a development shell:

```bash
nix develop
mkdir build && cd build
cmake .. -GNinja
ninja
./qwavrec
```

### Without Nix

Requires Qt 6 (Core, Gui, Widgets, Multimedia) and CMake ≥ 3.16.

```bash
mkdir build && cd build
cmake ..
cmake --build .
./qwavrec
```

## License

GPL-3.0-or-later. See [LICENSES/GPL-3.0-or-later.txt](LICENSES/GPL-3.0-or-later.txt).

This project follows the [REUSE](https://reuse.software/) specification.
