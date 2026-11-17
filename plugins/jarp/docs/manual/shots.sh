#!/usr/bin/env bash
# Renders the manual's panel pictures into img/: jarp-screenshot at twice the design size
# (1800 x 1486), each set up to show one part of JArp off, cropped to that part and saved as JPEG.
#    docs/manual/shots.sh path/to/jarp-screenshot
# Build the tool with -DJCLAP_BUILD_TOOLS=ON. Needs Python 3 with Pillow.
set -euo pipefail
tool=$(realpath "$1")
cd "$(dirname "$0")"
mkdir -p img
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# name, then the crop in design pixels (x y w h), then the tool's arguments.
shots=(
   "panel      0   0   900 743"
   "header     0   0   900 56"
   "grid       12  76  876 480"
   "rows-oct   12  76  876 360  preset=Night_Drive keys=41,45,48,52"
   "degree     12  76  876 360  rows=1 keys=48,55"
   "held-deg   12  76  876 360  rows=2 keys=48,55"
   "spread     12  76  876 360  rows=3 keys=36,43,48,52,55,60,64"
   "settings   12  566 876 148"
   "held       12  76  876 360  held bounce repeatends"
   "length-box 172 566 206 148  held bounce repeatends"
   "dice       520 70  368 488  dice"
   "long       12  76  876 370  long"
   "note       12  76  876 480  note"
   "picks      12  76  876 480  picks"
   "drums      12  76  876 360  preset=Four_on_the_Floor keys=36,38,42,46"
)
for line in "${shots[@]}"; do
   read -r name x y w h rest <<< "$line"
   args=()
   for a in $rest; do args+=("${a//_/ }"); done
   "$tool" "$tmp/$name.png" 1800 1486 "${args[@]}"
   python3 - "$tmp/$name.png" "img/$name.jpg" "$x" "$y" "$w" "$h" <<'PY'
import sys
from PIL import Image
src, out, x, y, w, h = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:])
Image.open(src).convert("RGB").crop((2 * x, 2 * y, 2 * (x + w), 2 * (y + h))).save(out, quality=86, optimize=True)
PY
done
echo "wrote $(pwd)/img"
