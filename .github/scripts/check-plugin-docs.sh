#!/usr/bin/env bash
# Fails when a pull request changes what a plugin does without touching the docs that describe it.
#
#    .github/scripts/check-plugin-docs.sh <base-ref> [pr-body-file]
#
# A plugin's source is plugins/<name>/src/** and plugins/<name>/m4l/**. When that changes:
#  - plugins/<name>/README.md must change too.
#  - If the plugin has a manual (plugins/<name>/docs/manual/), something in docs/manual/ or the
#    built PDF in docs/ must change too.
#  - If docs/manual/manual.html changes, the built PDF must change with it, so the shipped manual
#    never lags its source.
#
# Some changes really don't touch anything a reader sees (a refactor, a fix that only makes the
# plugin do what the docs already say). For those, put a line in the PR description:
#    Docs unchanged: jverb, jbreath - refactor only
# naming each plugin (or "all"), and the check lets them through.
set -euo pipefail
base=${1:?usage: check-plugin-docs.sh <base-ref> [pr-body-file]}
body=${2:-}

changed=$(git diff --name-only "$(git merge-base "$base" HEAD)" HEAD)
waived=""
if [ -n "$body" ] && [ -f "$body" ]; then
   waived=$(grep -i -E '^[[:space:]]*docs unchanged:' "$body" | sed -E 's/^[^:]*://' | tr 'A-Z,' 'a-z ' || true)
fi

is_waived() { for w in $waived; do [ "$w" = "$1" ] || [ "$w" = all ] && return 0; done; return 1; }
touched() { grep -q -E "$1" <<<"$changed"; }

fail=0
for dir in plugins/*/; do
   p=$(basename "$dir")
   [ "$p" = _template ] && continue
   ok=1
   pdf=$(ls "$dir"docs/*-User-Manual.pdf 2>/dev/null | head -1 || true)
   pdf_re=${pdf:+"^$(sed 's/[.]/\\./g' <<<"$pdf")\$"}

   if [ -n "$pdf" ] && touched "^plugins/$p/docs/manual/manual\.html$" && ! touched "$pdf_re"; then
      echo "::error::$p: docs/manual/manual.html changed but $pdf wasn't rebuilt (run docs/manual/build.sh)."
      fail=1; ok=0
   fi

   touched "^plugins/$p/(src|m4l)/" || continue
   if is_waived "$p"; then
      echo "$p: source changed, docs waived in the PR description."
      continue
   fi
   if ! touched "^plugins/$p/README\.md$"; then
      echo "::error::$p: source changed but plugins/$p/README.md didn't. Update it, or add 'Docs unchanged: $p - <why>' to the PR description."
      fail=1; ok=0
   fi
   if [ -d "${dir}docs/manual" ] && ! touched "^plugins/$p/docs/manual/" && ! touched "$pdf_re"; then
      echo "::error::$p: source changed but its manual (plugins/$p/docs/manual) didn't. Update it and rebuild the PDF, or add 'Docs unchanged: $p - <why>' to the PR description."
      fail=1; ok=0
   fi
   [ $ok = 1 ] && echo "$p: source and docs both changed."
done
exit $fail
