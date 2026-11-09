#!/usr/bin/env bash
# Renders the README's pictures: jbreath-screenshot at twice the design size (3296 x 1880), each with
# the view and settings that show its part of the panel off, cropped to that part and saved as JPEG.
#    docs/readme/make.sh path/to/jbreath-screenshot
# Needs Python 3 with Pillow.
set -euo pipefail
tool=$(realpath "$1")
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# name player-tab routes armed exciter-tab voice-tab air, the crop in design pixels (x y w h), then
# the settings (dice opens the dice card). The cards: exciter 16 64 880 332, bore 16 406 880 316,
# player 16 732 880 196, the loops 912 64 720 530 (with their chips), voice 904 612 728 316.
shots=(
   "panel    0 0 0 0 0 0   0   0   1648 940  -"
   "exciter  0 0 0 0 0 0   10  58  892  344  -"
   "hybrid   0 0 0 1 0 0   10  58  892  344  preset=Pastoral Hybrid_Wave=Saw Hybrid_Mix=35 Hybrid_Octave=1"
   "alien    0 0 0 2 0 0   10  58  892  344  preset=Alien_Speech Alien_Words=Typed lyrics=Hello,_I'm_an_alien._Take_me_to_your_leader. Alien_Flip=20"
   "map      0 0 2 0 0 0   0   0   1648 404  -"
   "bore     0 0 0 0 0 0   10  400 892  328  preset=Brass_Plume"
   "player   0 0 0 0 0 0   10  726 892  208  Tongue=Flutter Humanize=30"
   "vibrato  1 0 0 0 0 0   10  726 892  208  Vibrato_Type=Breath+Jaw"
   "lfos     2 0 0 0 0 0   10  726 892  208  -"
   "buses    3 0 0 0 0 0   10  726 892  208  -"
   "loops    0 0 0 0 0 0   906 58  732  542  preset=Smoke_Tenor"
   "routes   0 1 0 0 0 0   906 58  732  542  -"
   "voice    0 0 0 0 0 0   898 606 740  328  -"
   "air      0 0 0 0 1 0   898 606 740  328  Breath_Source=Mix"
   "room     0 0 0 0 2 0   898 606 740  328  Room=35"
   "delay    0 0 0 0 3 0   898 606 740  328  Delay=On Delay_Sync=1/8D Delay_Feedback=55"
   "air-view 0 0 0 0 0 1   0   0   1648 940  preset=Smoke_Tenor"
   "ink      0 0 0 0 0 1   0   0   1648 940  preset=Gutbucket Look=Ink Palette=Ember"
   "dice     0 0 0 0 0 0   406 4   1232 318  preset=Smoke_Tenor dice"
)
for line in "${shots[@]}"; do
   read -r name player routes armed exciter voice air x y w h settings <<< "$line"
   args=("$tmp/$name.png" 3296 1880 150 "$player" "$routes" "$armed" "$exciter" "$voice" "$air")
   [ "$settings" != "-" ] && read -r -a extra <<< "$settings" && args+=("${extra[@]}")
   "$tool" "${args[@]}"
   python3 - "$tmp/$name.png" "$name.jpg" "$x" "$y" "$w" "$h" <<'PY'
import sys
from PIL import Image
src, out, x, y, w, h = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:])
Image.open(src).convert("RGB").crop((2 * x, 2 * y, 2 * (x + w), 2 * (y + h))).save(out, quality=86, optimize=True)
PY
   echo "wrote $name.jpg"
done
