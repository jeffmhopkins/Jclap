#!/bin/bash
# Installs the JClap plugins on macOS: every CLAP, and the VST3s.
#
#    curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/install-macos.sh | bash
#
# installs the latest release; add "-s v0.14.0" after bash for a given one. Files fetched with curl
# aren't quarantined, so the plugins load without notarization. Each bundle replaces the old copy
# outright, since unzipping over an old bundle can leave stale files that stop it loading, and
# Bitwig's CLAP cache is cleared so its next scan starts fresh.
set -euo pipefail

version=${1:-latest}
if [ "$version" = latest ]; then
   base=https://github.com/jeffmhopkins/Jclap/releases/latest/download
else
   base=https://github.com/jeffmhopkins/Jclap/releases/download/$version
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# install <zip> <extension> <folder>
install() {
   echo "Downloading $1"
   curl -fsSL "$base/$1" -o "$tmp/$1"
   mkdir -p "$tmp/${1%.zip}" "$3"
   ditto -x -k "$tmp/$1" "$tmp/${1%.zip}"
   for p in "$tmp/${1%.zip}"/*."$2"; do
      rm -rf "$3/$(basename "$p")"
      mv "$p" "$3/"
      echo "  installed $3/$(basename "$p")"
   done
}

install jclap-macOS.zip clap ~/Library/Audio/Plug-Ins/CLAP
install jclap-VST3-macOS.zip vst3 ~/Library/Audio/Plug-Ins/VST3
find ~/Library/Caches/Bitwig -maxdepth 1 -name 'clap-metadata-*' -exec rm -rf {} + 2>/dev/null || true

echo "Done. Restart Bitwig (or your DAW) to pick them up."
