#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="${PVZ_WEB_BUILD_DIR:-${repo_dir}/build-web}"
engine_revision="cc9c47f0ca6a20d9c2b5969bce4f53a9f55518d8"
openmpt_revision="libopenmpt-0.8.4"
openmpt_commit="327204eaa2c27bd1acbb571babe68f279c85238b"
jobs="${PVZ_WEB_JOBS:-4}"

for tool in emcc emmake emcmake em-config cmake ninja git python3 make; do
  command -v "$tool" >/dev/null || { echo "Missing $tool. Activate Emscripten 4.0.21 and install CMake, Ninja, Python 3 and make." >&2; exit 1; }
done
mkdir -p "$work_dir"
engine_dir="$work_dir/engine"
music_dir="$work_dir/openmpt"
stage_dir="$work_dir/stage"
build_dir="$work_dir/build"
dist_dir="$work_dir/dist"
music_prefix="$work_dir/openmpt-prefix"

if [[ ! -d "$engine_dir/.git" ]]; then
  git clone --no-checkout https://github.com/wszqkzqk/PvZ-Portable.git "$engine_dir"
fi
git -C "$engine_dir" cat-file -e "$engine_revision^{commit}" 2>/dev/null || git -C "$engine_dir" fetch origin "$engine_revision"
git -C "$engine_dir" checkout --detach "$engine_revision"
[[ "$(git -C "$engine_dir" rev-parse HEAD)" == "$engine_revision" ]]

if [[ ! -d "$music_dir/.git" ]]; then
  git clone --depth 1 --branch "$openmpt_revision" https://github.com/OpenMPT/openmpt.git "$music_dir"
fi
git -C "$music_dir" cat-file -e "$openmpt_commit^{commit}" 2>/dev/null || git -C "$music_dir" fetch origin "$openmpt_commit"
git -C "$music_dir" checkout --detach "$openmpt_commit"
if [[ ! -f "$music_prefix/lib/libopenmpt.a" ]]; then
  music_options=(CONFIG=emscripten STATIC_LIB=1 SHARED_LIB=0 EXAMPLES=0 OPENMPT123=0 TEST=0
    NO_ZLIB=1 NO_MPG123=1 NO_OGG=1 NO_VORBIS=1 NO_VORBISFILE=1 NO_PORTAUDIO=1 NO_PORTAUDIOCPP=1
    NO_PULSEAUDIO=1 NO_SDL2=1 NO_FLAC=1 NO_SNDFILE=1)
  emmake make -C "$music_dir" "${music_options[@]}" -j"$jobs"
  mkdir -p "$music_prefix/lib" "$music_prefix/include"
  cp "$music_dir/bin/libopenmpt.a" "$music_prefix/lib/"
  cp -R "$music_dir/libopenmpt" "$music_prefix/include/"
fi

python3 "$repo_dir/web/prepare_sources.py" --engine "$engine_dir" --output "$stage_dir"
python3 "$repo_dir/web/package_assets.py" "$repo_dir/Debug" "$stage_dir/assets"
emcmake cmake -S "$stage_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPVZ_DEBUG=OFF -DLIMBO_PAGE=OFF \
  -DOPENMPT_LIBRARY="$music_prefix/lib/libopenmpt.a" \
  -DOpenMPT_LIBRARY="$music_prefix/lib/libopenmpt.a" \
  -DOpenMPT_INCLUDE_DIR="$music_prefix/include" \
  -DCMAKE_CXX_FLAGS="-I${music_prefix}/include"
cmake --build "$build_dir" --parallel "$jobs"

mkdir -p "$dist_dir/licenses/portable" "$dist_dir/licenses/openmpt"
for name in index.html index.js index.wasm index.data; do
  test -s "$build_dir/$name"
  cp "$build_dir/$name" "$dist_dir/"
done
cp "$engine_dir/LICENSE" "$engine_dir/COPYING" "$dist_dir/licenses/portable/"
cp "$engine_dir/src/SexyAppFramework/LICENSE" "$dist_dir/licenses/portable/PopCap.txt"
cp "$music_dir/LICENSE" "$dist_dir/licenses/openmpt/"
python3 "$repo_dir/web/package_notices.py" "$(em-config CACHE)/ports" \
  "$engine_dir/src/SexyAppFramework/sound/SDL-Mixer-X" "$dist_dir/licenses/codecs"
cp "$repo_dir/web/THIRD_PARTY.md" "$dist_dir/licenses/README.md"
cp "$stage_dir/assets/asset-manifest.json" "$dist_dir/asset-manifest.json"
touch "$dist_dir/.nojekyll"
git -C "$repo_dir" rev-parse HEAD > "$dist_dir/source-revision.txt"
printf 'Built PVZ-TFT WebAssembly: %s\n' "$dist_dir"



