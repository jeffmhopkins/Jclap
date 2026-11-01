#!/bin/bash
# Installs the JClap plugins on macOS: every CLAP, and the VST3s.
#
#    curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/install-macos.sh | bash
#
# installs the latest release; add "-s v0.17.0" after bash for a given one. Files fetched with curl
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

# fetch <zip>: download a zip from the release and unpack it; prints where. Fails if it isn't there.
fetch() {
   echo "Downloading $1" >&2
   curl -fsSL "$base/$1" -o "$tmp/$1" || return 1
   mkdir -p "$tmp/${1%.zip}"
   ditto -x -k "$tmp/$1" "$tmp/${1%.zip}"
   echo "$tmp/${1%.zip}"
}

# install <folder> <extension> <plug-in folder>: move every bundle in <folder> into place.
install() {
   mkdir -p "$3"
   for p in "$1"/*."$2"; do
      [ -e "$p" ] || continue
      rm -rf "$3/$(basename "$p")"
      mv "$p" "$3/"
      echo "  installed $3/$(basename "$p")"
   done
}

clap=~/Library/Audio/Plug-Ins/CLAP
vst3=~/Library/Audio/Plug-Ins/VST3
dir=$(fetch jclap-macOS.zip)
if [ -d "$dir/CLAP" ]; then
   # Newer releases: one zip with CLAP/ and VST3/ folders.
   install "$dir/CLAP" clap "$clap"
   install "$dir/VST3" vst3 "$vst3"
else
   # Earlier releases: the CLAPs at the top of the zip, and the VST3s in a zip of their own.
   install "$dir" clap "$clap"
   if dir=$(fetch jclap-VST3-macOS.zip); then install "$dir" vst3 "$vst3"; fi
fi
find ~/Library/Caches/Bitwig -maxdepth 1 -name 'clap-metadata-*' -exec rm -rf {} + 2>/dev/null || true

echo "Done. Restart Bitwig (or your DAW) to pick them up."
