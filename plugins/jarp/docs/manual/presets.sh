#!/usr/bin/env bash
# Regenerates presets.js (the manual's preset list) from the factory library in src/library.h.
# Run it whenever a preset or blurb changes, then build.sh.
#    docs/manual/presets.sh [path/to/clap/include]
# Needs a C++17 compiler and the CLAP headers: by default the ones CMake fetched into build/_deps.
set -euo pipefail
cd "$(dirname "$0")"
root=$(cd ../../../.. && pwd)
clap=${1:-$root/build/_deps/clap-src/include}
[ -f "$clap/clap/clap.h" ] || { echo "CLAP headers not found in $clap (configure the build first, or pass the path)" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CXX:-c++} -std=c++17 -O1 -I../../src -I"$root/libs/core/include" -I"$clap" tools/presets.cpp -o "$tmp/presets"
"$tmp/presets" > presets.js
echo "wrote $(pwd)/presets.js ($(grep -c '^    \[' presets.js) presets)"
