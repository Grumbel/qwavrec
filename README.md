<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# QWavRec

A small, traditional Qt desktop application for playing and recording audio via PipeWire (through Qt Multimedia).

It is essentially a GUI for the common `pw-play` / `pw-record` use cases:

* Select audio input or output device
* Record audio to a file (WAV by default)
* Play an audio file
* Switch devices without restarting the application
* Quickly check that a microphone or speaker works

## Features (MVP)

* Input / Output device selectors with hot-plug support
* File selection for playback or recording target
* Record / Play / Stop
* Seek slider and time display for playback
* Basic error reporting

## Building

### With Nix

```bash
nix build
./result/bin/qwavrec
```

Or enter a development shell:

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
