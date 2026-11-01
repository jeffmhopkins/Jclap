#!/bin/bash
# Packs the release zips from what each system's build staged, on macOS (ditto keeps the bundles intact).
#
#    .github/package.sh <stage> <dist>
#
# <stage>/<system>/CLAP/*.clap and <stage>/<system>/VST3/*.vst3 come from the build job, one folder per
# system (macOS, Windows, Linux). It writes two kinds of zip to <dist>:
#
#    jclap-<system>.zip              every plugin for one system: CLAP/, VST3/, and on macOS and
#                                    Windows "Max for Live/" (Live doesn't run on Linux)
#    jclap-<Plugin>-<version>.zip    one plugin for every system: macOS/, Windows/, Linux/, each with
#                                    its CLAP (and VST3), plus "Max for Live/" for a plugin that has one
#
# The system zips keep fixed names, so the installer and "latest" links always find them.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
stage=$(cd "$1" && pwd)
mkdir -p "$2"
dist=$(cd "$2" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
systems="macOS Windows Linux"

# zip <folder> <zip>: the folder's contents (not the folder itself), without macOS metadata.
zip_dir() { ditto -c -k --norsrc --noextattr --noqtn --noacl "$1" "$2"; }

# m4l <folder> <dest>: copy a plugin's Max for Live device (the .amxd and the scripts it loads) into
# <dest>/<device name>/. Nothing if the plugin has none.
m4l() {
   local src="$here/plugins/$1/m4l" amxd name f
   [ -d "$src" ] || return 0
   for amxd in "$src"/*.amxd; do
      name=$(basename "$amxd" .amxd)
      mkdir -p "$2/$name"
      cp "$amxd" "$2/$name/"
      for f in "$src"/*.js; do
         case $(basename "$f") in test_*) ;; *) cp "$f" "$2/$name/" ;; esac
      done
   done
}

for os in $systems; do
   [ -d "$stage/$os" ] || { echo "No build staged for $os" >&2; exit 1; }
done

plugins=$("$here/.github/plugin-versions.sh" list)

# By system.
for os in $systems; do
   mkdir -p "$work/system/$os"
   ditto "$stage/$os" "$work/system/$os"
   if [ "$os" != Linux ]; then
      while read -r d name version; do m4l "$d" "$work/system/$os/Max for Live"; done <<< "$plugins"
   fi
   zip_dir "$work/system/$os" "$dist/jclap-$os.zip"
done

# By plugin.
while read -r d name version; do
   out="$work/plugin/$name"
   for os in $systems; do
      mkdir -p "$out/$os"
      for ext in clap vst3; do
         dir=CLAP; [ "$ext" = vst3 ] && dir=VST3
         if [ -e "$stage/$os/$dir/$name.$ext" ]; then
            ditto "$stage/$os/$dir/$name.$ext" "$out/$os/$name.$ext"
         fi
      done
      [ -e "$out/$os/$name.clap" ] || { echo "$name.clap is missing from the $os build" >&2; exit 1; }
   done
   m4l "$d" "$out/Max for Live"
   zip_dir "$out" "$dist/jclap-$name-$version.zip"
done <<< "$plugins"

ls -l "$dist"
