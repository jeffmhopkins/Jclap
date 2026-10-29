#!/usr/bin/env bash
# Renders the README's pictures: jsynth-screenshot at twice the design size (3296 x 1880), each with
# a factory preset that shows its part of the panel off, cropped to that part and saved as JPEG.
#    docs/readme/make.sh path/to/jsynth-screenshot
# Needs Python 3 with Pillow. Favorites are starred in a throwaway home folder, so the star shows lit.
set -euo pipefail
tool=$(realpath "$1")
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/home/.config/JSynth"
printf 'Starling Bells\nTwo Tides\nFlight Steps\nTwin Pulse\nMurmuration Riot\nTubular Bells\nFinger Wah\n' \
   > "$tmp/home/.config/JSynth/Favorites.txt"

# name part routes map-source view voice-tab env-tab osc-tab preset, then the crop in design pixels
# (x y w h). The cards: oscillators 16 64 880 330, filter 16 404 880 312, envelopes 16 727 880 196,
# the tabs under the sky 912 650 720 274, the sky 912 52 720 590.
shots=(
   "panel       0 0 0  0 0 0 0 -               0   0   1648 940"
   "fm          0 0 0  0 0 0 1 Tubular_Bells   10  58  892  342"
   "map-filter  0 0 22 0 0 0 0 Finger_Wah      10  398 892  324"
   "shape       0 0 0  0 0 0 0 Two_Tides       10  721 892  208"
   "lfos        0 0 0  0 0 1 0 Twin_Pulse      10  721 892  208"
   "buses       0 0 0  0 1 2 0 Flight_Steps    10  721 892  208"
   "flock       0 0 0  0 1 2 0 Flight_Steps    906 644 732  286"
   "motion      0 0 0  0 2 0 0 Murmuration_Riot 906 644 732 286"
   "routes      0 1 0  0 0 0 0 Flight_Steps    906 46  732  602"
   "sky         0 0 0  1 0 0 0 Murmuration_Riot 0  0   1648 940"
)
for line in "${shots[@]}"; do
   read -r name part routes map view voice env osc preset x y w h <<< "$line"
   args=("$tmp/$name.png" "$part" "$routes" "$map" 3296 1880 "$view" "$voice" "$env" "$osc")
   [ "$preset" != "-" ] && args+=("${preset//_/ }")
   HOME="$tmp/home" XDG_CONFIG_HOME="" "$tool" "${args[@]}"
   python3 - "$tmp/$name.png" "$name.jpg" "$x" "$y" "$w" "$h" <<'PY'
import sys
from PIL import Image
src, out, x, y, w, h = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:])
Image.open(src).convert("RGB").crop((2 * x, 2 * y, 2 * (x + w), 2 * (y + h))).save(out, quality=86, optimize=True)
PY
   echo "wrote $name.jpg"
done

# The dice card, open over the sky's top right (the tool's last argument opens it).
HOME="$tmp/home" XDG_CONFIG_HOME="" "$tool" "$tmp/dice.png" 0 0 0 3296 1880 0 0 0 0 Murmuration\ Riot 0 0 1 1
python3 - "$tmp/dice.png" dice.jpg <<'PY'
import sys
from PIL import Image
x, y, w, h = 1322, 62, 318, 208
Image.open(sys.argv[1]).convert("RGB").crop((2 * x, 2 * y, 2 * (x + w), 2 * (y + h))).save(sys.argv[2], quality=86, optimize=True)
PY
echo "wrote dice.jpg"
