# PVZ-TFT WebAssembly: third-party components

The web target retains the PVZ-TFT gameplay sources from this repository. It stages a platform backend from [PvZ-Portable](https://github.com/wszqkzqk/PvZ-Portable) at commit `cc9c47f0ca6a20d9c2b5969bce4f53a9f55518d8` and applies the adapters in `web/prepare_sources.py`. Desktop sources are not replaced.

PvZ-Portable contributions are LGPL-3.0-or-later; portions of the underlying framework retain the PopCap Games Framework license. The distribution includes those texts under `licenses/portable/`. Source, modifications and reproducible build instructions are available from the linked repositories and this repository's `web/` directory. Rebuild with modified sources using `bash web/build.sh`.

Music playback uses libopenmpt 0.8.4 (BSD-3-Clause); its license is included under `licenses/openmpt/`. SDL2, SDL-Mixer-X, Emscripten and the image/audio codec ports retain their respective upstream licenses. The build disables optional GPL and LGPL mixer codecs; this does not change the license of the portable engine.

The game resources come from this repository's `Debug/` directories. This document does not grant additional rights to those resources or change their ownership.
