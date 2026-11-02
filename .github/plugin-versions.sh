#!/bin/bash
# Each plugin's own version (jclap_plugin_version in plugins/<folder>/CMakeLists.txt), for releases.
#
#    .github/plugin-versions.sh list [<ref>]       "<folder> <file name> <version>" per shipped plugin
#    .github/plugin-versions.sh check <level>      plugins changed since the last release with the same version
#    .github/plugin-versions.sh notes <tag>        the release notes, from .github/release-notes.md
#    .github/plugin-versions.sh tag <release>      the tag for a release number: 20 gives v20.0.0
#    .github/plugin-versions.sh name <tag>         a release's name: v20.0.0 (or v0.19.0) gives Release 20
#
# Releases are counted, Release 19, Release 20 and so on, so their number never looks like a plugin's
# version. Release N is tagged vN.0.0: plugins already installed read the newest release's tag as
# three numbers, and the count carries on from the v0.N.0 tags before it (v0.19.0 was Release 19).
#
# list with a ref (a release tag) lists the plugins as they were there; a plugin from before plugins
# had their own versions gets that release's version (project() in CMakeLists.txt). check prints
# each problem as a GitHub annotation at <level> (warning or error), and fails only for error.
set -euo pipefail
cd "$(dirname "$0")/.."

# read <path> [<ref>]: a file now, or at a ref. Empty if it isn't there.
read_file() {
   if [ -n "${2:-}" ]; then
      git show "$2:$1" 2>/dev/null | tr -d '\r' || true
   else
      tr -d '\r' < "$1" 2>/dev/null || true
   fi
}

list() {
   local ref=${1:-} dirs suite d text name version
   if [ -n "$ref" ]; then
      dirs=$(git ls-tree --name-only "$ref" plugins/ | sed 's|^plugins/||')
   else
      dirs=$(ls plugins)
   fi
   suite=$(read_file CMakeLists.txt "$ref" | sed -n 's/^project(jclap VERSION \([0-9.]*\).*/\1/p')
   for d in $dirs; do
      [ "$d" = _template ] && continue
      text=$(read_file "plugins/$d/CMakeLists.txt" "$ref")
      [ -n "$text" ] || continue
      name=$(sed -n 's/^ *OUTPUT_NAME "\([^"]*\)".*/\1/p' <<< "$text" | head -n 1)
      version=$(sed -n 's/^jclap_plugin_version(\([0-9.]*\)).*/\1/p' <<< "$text")
      echo "$d $name ${version:-$suite}"
   done
}

# The newest release tag before this commit (one on this commit is the release being made).
previous_release() {
   local here
   here=$(git tag --points-at HEAD)
   git tag --list 'v[0-9]*' --sort=-v:refname --merged HEAD | grep -vxF -e "$here" | head -n 1 || true
}

# The version <folder> had in a `list` output.
version_in() { awk -v d="$1" '$1 == d { print $3 }' <<< "$2"; }

check() {
   local level=$1 prev old d name version was failed=0
   prev=$(previous_release)
   if [ -z "$prev" ]; then
      echo "No earlier release to compare with."
      return 0
   fi
   old=$(list "$prev")
   while read -r d name version; do
      was=$(version_in "$d" "$old")
      [ -n "$was" ] || continue # new since then
      # Docs and screenshots don't change what ships.
      if [ "$version" = "$was" ] && ! git diff --quiet "$prev" HEAD -- "plugins/$d" \
            ":(exclude)plugins/$d/docs" ":(exclude)plugins/$d/*.md"; then
         echo "::$level file=plugins/$d/CMakeLists.txt::$name changed since $(release_name "$prev") but is still $version." \
            "Bump jclap_plugin_version in plugins/$d/CMakeLists.txt."
         failed=1
      fi
   done <<< "$(list)"
   if [ "$failed" = 1 ] && [ "$level" = error ]; then
      return 1
   fi
   [ "$failed" = 1 ] || echo "Every plugin changed since $(release_name "$prev") has a new version."
}

# The formats a plugin ships in, from its folder: CLAP always, VST3 when it builds one, and Max for
# Live when it has a device.
formats() {
   local f=CLAP
   grep -q '^ *jclap_add_vst3(' "plugins/$1/CMakeLists.txt" && f="$f, VST3"
   [ -d "plugins/$1/m4l" ] && f="$f, Max for Live"
   echo "$f"
}

# The table of plugins with their versions and zips (as .github/package.sh names them), and the line
# the update check reads (libs/ui/src/updates.cpp).
table() {
   local tag=$1 prev old d name version was zip pairs=""
   prev=$(previous_release)
   old=$([ -n "$prev" ] && list "$prev" || true)
   echo "| Plugin | Version | Formats | Zip |"
   echo "|---|---|---|---|"
   while read -r d name version; do
      was=$(version_in "$d" "$old")
      zip="jclap-$name-$version.zip"
      if [ -n "$prev" ] && [ "$version" != "$was" ]; then
         version="**$version** (new since $(release_name "$prev"))"
      fi
      echo "| $name | $version | $(formats "$d") | [$zip](https://github.com/jeffmhopkins/Jclap/releases/download/$tag/$zip) |"
   done <<< "$(list)"
   while read -r d name version; do pairs="$pairs $d=$version"; done <<< "$(list)"
   echo
   echo "<!-- jclap-versions:$pairs -->"
}

# The release number in a tag (v20.0.0 and v0.19.0 give 20 and 19), or nothing for any other tag.
release_number() {
   if [[ $1 =~ ^v([1-9][0-9]*)\.0\.0$ ]] || [[ $1 =~ ^v0\.([1-9][0-9]*)\.0$ ]]; then
      echo "${BASH_REMATCH[1]}"
   fi
}

release_name() {
   local n
   n=$(release_number "$1")
   if [ -n "$n" ]; then echo "Release $n"; else echo "$1"; fi
}

release_tag() {
   local n=${1#Release }
   n=${n#release }
   if ! [[ $n =~ ^[1-9][0-9]*$ ]]; then
      echo "A release is a number, like 20 (not \"$1\")." >&2
      return 1
   fi
   echo "v$n.0.0"
}

notes() {
   local tag=$1 line release
   release=$(release_number "$tag")
   release=${release:-$tag}
   while IFS= read -r line; do
      if [ "$line" = "{{PLUGINS}}" ]; then
         table "$tag"
      elif [ "$line" = "{{INTRO}}" ]; then
         # Only a numbered release says what its number is (not the dev build).
         if [ "$release" != "$tag" ]; then
            echo "**Release $release** is the number of this JClap release, counted one per release. It isn't a"
            echo "plugin version: each plugin has its own, listed below, and that's the one the plugin shows."
            echo
         fi
      else
         line=${line//'{{VERSION}}'/$tag}
         echo "${line//'{{RELEASE}}'/$release}"
      fi
   done < .github/release-notes.md
}

case "${1:-}" in
   list) list "${2:-}" ;;
   check) check "${2:-error}" ;;
   notes) notes "$2" ;;
   tag) release_tag "${2:-}" ;;
   name) release_name "${2:-}" ;;
   *) echo "usage: $0 list [<ref>] | check <warning|error> | notes <tag> | tag <release> | name <tag>" >&2; exit 2 ;;
esac
