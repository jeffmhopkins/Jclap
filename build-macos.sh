#!/bin/bash
# Builds the JClap plugins from source on macOS and installs them: one command, no download of a
# release needed.
#
#    curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/build-macos.sh | bash
#
# builds and installs every plugin from the latest code on main. Name plugins to build only those
# (their folder names under plugins/), which is much quicker:
#
#    curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/build-macos.sh | bash -s polar-xy
#
# The source goes in ~/Jclap-src (JCLAP_SRC sets another folder) and is updated on each run, so
# running it again rebuilds only what changed. It needs Apple's command line tools (xcode-select
# --install), git and CMake (brew install cmake, or cmake.org). The build is for this Mac only (Apple
# Silicon or Intel), signed ad hoc so it loads; Bitwig's CLAP cache is cleared so its next scan
# starts fresh.
set -euo pipefail

src=${JCLAP_SRC:-$HOME/Jclap-src}
repo=https://github.com/jeffmhopkins/Jclap.git

if ! xcode-select -p >/dev/null 2>&1; then
   echo "Apple's command line tools are needed: run xcode-select --install, then run this again." >&2
   exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
   echo "CMake is needed: brew install cmake (or get it from https://cmake.org/download/), then run this again." >&2
   exit 1
fi

if [ -d "$src/.git" ]; then
   echo "Updating $src"
   git -C "$src" pull --ff-only
else
   echo "Getting the source into $src"
   git clone --depth 1 "$repo" "$src"
fi
cd "$src"

# What to build, and the bundles it makes (each plugin's OUTPUT_NAME in its CMakeLists.txt).
targets=()
names=()
for p in "$@"; do
   if [ ! -f "plugins/$p/CMakeLists.txt" ]; then
      echo "No plugin called $p. The plugins are: $(ls plugins | grep -v '^_' | tr '\n' ' ')" >&2
      exit 1
   fi
   targets+=("$p")
   grep -q "jclap_add_vst3($p-vst3" "plugins/$p/CMakeLists.txt" && targets+=("$p-vst3")
   names+=("$(grep -m1 'OUTPUT_NAME' "plugins/$p/CMakeLists.txt" | sed -E 's/.*"([^"]+)".*/\1/')")
done

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
if [ ${#targets[@]} -gt 0 ]; then
   cmake --build build --config Release --parallel "$jobs" --target "${targets[@]}"
else
   cmake --build build --config Release --parallel "$jobs"
fi

# install <bundle> <plug-in folder>: sign it and put it in place, replacing the old copy outright.
install() {
   [ -e "$1" ] || return 0
   codesign --force --sign - "$1" >/dev/null 2>&1 || true
   mkdir -p "$2"
   rm -rf "$2/$(basename "$1")"
   cp -R "$1" "$2/"
   echo "  installed $2/$(basename "$1")"
}

clap=~/Library/Audio/Plug-Ins/CLAP
vst3=~/Library/Audio/Plug-Ins/VST3
if [ ${#names[@]} -gt 0 ]; then
   for n in "${names[@]}"; do
      install "build/clap/$n.clap" "$clap"
      install "build/vst3/$n.vst3" "$vst3"
   done
else
   for b in build/clap/*.clap; do install "$b" "$clap"; done
   for b in build/vst3/*.vst3; do install "$b" "$vst3"; done
fi
find ~/Library/Caches/Bitwig -maxdepth 1 -name 'clap-metadata-*' -exec rm -rf {} + 2>/dev/null || true

echo "Done. Restart Bitwig (or your DAW) to pick them up."
