# PVZ-TFT on WebAssembly / GitHub Pages

This target compiles the repository's TFT gameplay to WebAssembly. It uses a pinned PvZ-Portable platform backend for WebGL, SDL audio, the browser event loop and filesystem access. `web/prepare_sources.py` stages the original `Lawn/`, `LawnApp`, resource IDs and constants; it does not substitute the upstream game's board or rules. The Windows project stays available.

## Build

Install and activate **Emscripten 4.0.21**, plus CMake, Ninja, Python 3, make and git. On Ubuntu, `sudo apt-get install cmake ninja-build python3 make git` installs the native build tools.

```bash
source /path/to/emsdk/emsdk_env.sh
bash web/build.sh
python3 -m http.server 8080 --directory build-web/dist
```

Open `http://localhost:8080/`. Opening `index.html` directly as a local file does not load the Wasm/data bundle correctly. The first build also builds libopenmpt for Emscripten. Dependencies are pinned; later builds reuse the music library. Set `PVZ_WEB_JOBS` to adjust build parallelism and `PVZ_WEB_BUILD_DIR` to use a separate build directory.

The output is `build-web/dist/`: `index.html`, `index.js`, `index.wasm`, `index.data`, asset hashes and license information. Only the eight resource directories selected in `package_assets.py` are bundled. Local saves, executable files, DLLs and tools in `Debug/` are excluded.

## Publish on GitHub Pages

The workflow `.github/workflows/webassembly-pages.yml` builds and runs tests for PRs. It uploads a downloadable `pvz-tft-web` artifact. Pushes to `main` and manual runs on `main` deploy the tested output to Pages.

In **Settings → Pages → Build and deployment → Source**, select **GitHub Actions** once. The expected project URL is `https://manikopvz.github.io/PVZ-TFT/`; the action's deployment output is authoritative. The repository can remain private if its GitHub plan supports Pages for private repositories. If GitHub reports that the plan does not support Pages, that account setting must be resolved before deployment can succeed. Do not make the repository public just to run the build.

No SharedArrayBuffer, pthreads, cross-origin-isolation headers, backend server or external CDN is needed to play. The HTML and generated loader use relative paths, including beneath `/PVZ-TFT/`.

## Controls and saves

The page contains only the game canvas, a centered play icon after loading, and keyboard/fullscreen icons in the upper corner. Click the play icon to unlock browser audio and start the game. The renderer uses the original 1280×720 logical size with a matching 16:9 GL viewport and mouse mapping. Smaller screens fit the whole canvas without stretching; only unused space outside the canvas may be black.

Mouse and keyboard retain the game's controls. On touchscreens, tap for left-click, drag normally, or hold for 350 ms for right-click (two successive holds provide the Bench sell gesture).  On mobile, select the username field in the game and tap the keyboard icon to open the system keyboard. Text, deletion, paste and Enter go through the native EditWidget validation and length limit. The existing bitmap font supports ASCII usernames. Browser gamepad input uses SDL2 without consuming the engine's mouse/keyboard event queue.

Saves and configuration live under `/saves/` in IndexedDB and restore before play becomes available. The app checkpoints every 20 seconds and when the tab becomes hidden. Save/backup controls are no longer displayed. Advanced backup helpers remain available as `exportSave()` and `importSave(file)` in the browser console. Closing a tab can interrupt the last asynchronous write. Existing Windows saves are not promised compatible with this build's data layout.

Background music requires the statically linked OpenMPT decoder in SDL-Mixer-X, configured with its case-sensitive `OpenMPT_LIBRARY` and `OpenMPT_INCLUDE_DIR` variables. Configuration now fails if the decoder is omitted. The smoke test requires an active music stream and increasing nonzero decoded music PCM, independently of SFX.

The web target uses the bundled default resource pack. Desktop Discord Rich Presence, native font selection and resource-pack switching are unavailable in the browser build; their controls are hidden. Existing bitmap fonts are used for the debug font paths. The code changes do not add artwork.

## Validation

```bash
python3 -m unittest discover -s web/tests -v
npm install --prefix build-web/browser-test playwright@1.51.1
build-web/browser-test/node_modules/.bin/playwright install --with-deps chromium
NODE_PATH="$PWD/build-web/browser-test/node_modules" node web/tests/browser-smoke.cjs build-web/dist
```

The browser test serves the output under `/PVZ-TFT/`, starts the actual Wasm game, checks the full 1280×720 GL viewport, decoded music, mobile username edits, responsive aspect ratio and loading completion, verifies IndexedDB restoration after a reload, and downloads a save backup. GitHub Actions runs the same checks. These checks are a startup/save smoke test; they do not establish that every level, augment or gamepad has received a full playthrough.

See [THIRD_PARTY.md](THIRD_PARTY.md) for the pinned backend and license information. Source transformations are confined to the generated staging directory, so future TFT gameplay edits remain the source of truth for both targets.

