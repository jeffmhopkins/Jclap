#!/bin/bash
# Each plugin's own version (jclap_plugin_version in plugins/<folder>/CMakeLists.txt), for releases.
#
#    .github/plugin-versions.sh list [<ref>]       "<folder> <file name> <version>" per shipped plugin
#    .github/plugin-versions.sh check <level>      plugins changed since the last release with the same version
#    .github/plugin-versions.sh notes <tag>        the release notes, from .github/release-notes.md
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
         echo "::$level file=plugins/$d/CMakeLists.txt::$name changed since $prev but is still $version." \
            "Bump jclap_plugin_version in plugins/$d/CMakeLists.txt."
         failed=1
      fi
   done <<< "$(list)"
   if [ "$failed" = 1 ] && [ "$level" = error ]; then
      return 1
   fi
   [ "$failed" = 1 ] || echo "Every plugin changed since $prev has a new version."
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
         version="**$version** (new since $prev)"
      fi
      echo "| $name | $version | $(formats "$d") | [$zip](https://github.com/jeffmhopkins/Jclap/releases/download/$tag/$zip) |"
   done <<< "$(list)"
   while read -r d name version; do pairs="$pairs $d=$version"; done <<< "$(list)"
   echo
   echo "<!-- jclap-versions:$pairs -->"
}

notes() {
   local tag=$1 line
   while IFS= read -r line; do
      if [ "$line" = "{{PLUGINS}}" ]; then
         table "$tag"
      else
         echo "${line//'{{VERSION}}'/$tag}"
      fi
   done < .github/release-notes.md
}

case "${1:-}" in
   list) list "${2:-}" ;;
   check) check "${2:-error}" ;;
   notes) notes "$2" ;;
   *) echo "usage: $0 list [<ref>] | check <warning|error> | notes <tag>" >&2; exit 2 ;;
esac
