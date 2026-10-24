#!/usr/bin/env bash
# Builds ../JVerb-User-Manual.pdf from manual.html.
#
# Needs Node with Playwright (and its Chromium), poppler-utils (pdfinfo, pdftotext) and Python 3.
# Inter is fetched into fonts/ on the first run. The first pass prints hidden markers to find the
# page each heading lands on; the second fills those pages into the contents.
#
# The panel images in img/ are crops of 2400 x 1560 renders from jverb-screenshot.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -f fonts/Inter-Regular.ttf ]; then
   mkdir -p fonts
   tmp=$(mktemp -d)
   curl -sSL -o "$tmp/inter.zip" https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
   unzip -q -o "$tmp/inter.zip" -d "$tmp"
   for f in Inter-Regular Inter-Italic Inter-Medium Inter-SemiBold Inter-Bold \
            InterDisplay-Light InterDisplay-Regular InterDisplay-Medium InterDisplay-SemiBold; do
      cp "$tmp/extras/ttf/$f.ttf" fonts/
   done
   rm -rf "$tmp"
fi

tmp=$(mktemp -d)
node build.js marker "$tmp/pass1.pdf"
python3 - "$tmp/pass1.pdf" "$tmp/toc.json" <<'PY'
import json, re, subprocess, sys
pdf, out = sys.argv[1], sys.argv[2]
pages = int(re.search(r'Pages:\s+(\d+)', subprocess.run(['pdfinfo', pdf], capture_output=True, text=True).stdout).group(1))
toc = {}
for i in range(1, pages + 1):
    text = subprocess.run(['pdftotext', '-f', str(i), '-l', str(i), pdf, '-'], capture_output=True, text=True).stdout
    for key in re.findall(r'@@(.+?)@@', text.replace('\n', '')):
        toc.setdefault(key, i)
json.dump(toc, open(out, 'w'))
PY
node build.js final ../JVerb-User-Manual.pdf "$tmp/toc.json"
rm -rf "$tmp"
echo "wrote $(cd .. && pwd)/JVerb-User-Manual.pdf"
