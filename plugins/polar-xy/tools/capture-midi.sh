#!/bin/bash
# Captures a MIDI controller's raw messages, with exact timing, to a text file: for tuning Polar XY's
# Touch Filter to a pad. macOS (and Linux).
#
#    curl -fsSL https://raw.githubusercontent.com/jeffmhopkins/Jclap/main/plugins/polar-xy/tools/capture-midi.sh | bash
#
# or save it and run "bash capture-midi.sh". It lists the MIDI inputs and asks which to capture, then
# records every message until a key is pressed, and saves "midi-capture-<date>-<time>.txt" on the
# Desktop (or in the current folder if there's no Desktop).
#
# The first run sets up a small Python environment in ~/.jclap-midi-capture with python-rtmidi (from
# PyPI); later runs reuse it. It needs python3: on a Mac, if it's missing, macOS offers to install the
# command line tools when it's first run.
set -euo pipefail

ENV="$HOME/.jclap-midi-capture"

if ! command -v python3 >/dev/null 2>&1; then
   echo "python3 is needed. On a Mac, run: xcode-select --install   then run this again." >&2
   exit 1
fi

if [ ! -x "$ENV/bin/python" ] || ! "$ENV/bin/python" -c "import rtmidi" 2>/dev/null; then
   echo "Setting up (once): a Python environment with python-rtmidi in $ENV ..."
   rm -rf "$ENV"
   python3 -m venv "$ENV"
   "$ENV/bin/python" -m pip install --quiet --upgrade pip
   "$ENV/bin/python" -m pip install --quiet python-rtmidi
fi

OUT_DIR="$HOME/Desktop"
[ -d "$OUT_DIR" ] || OUT_DIR="$PWD"
OUT="$OUT_DIR/midi-capture-$(date +%Y%m%d-%H%M%S).txt"

# The capture itself, run with the terminal as its input even when this script was piped in from curl.
SCRIPT="$(mktemp -t jclap-midi-capture.XXXXXX)"
trap 'rm -f "$SCRIPT"' EXIT
cat > "$SCRIPT" <<'PY'
import os
import select
import sys
import termios
import time
import tty

import rtmidi

out_path = sys.argv[1]
midi_in = rtmidi.MidiIn()
ports = midi_in.get_ports()
if not ports:
    print("No MIDI inputs found. Plug the controller in and run this again.")
    sys.exit(1)

print("MIDI inputs:")
for i, name in enumerate(ports):
    print(f"  {i + 1}. {name}")
while True:
    choice = input(f"Capture which one? [1-{len(ports)}] ").strip()
    if choice.isdigit() and 1 <= int(choice) <= len(ports):
        port = int(choice) - 1
        break
    print("Type one of the numbers above.")

events = []
start = None


def on_message(message, _data):
    global start
    now = time.perf_counter()
    if start is None:
        start = now
    events.append((now - start, message[0]))


# Keep clock and active sensing out (they'd swamp the file); keep everything else.
midi_in.ignore_types(sysex=True, timing=True, active_sense=True)
midi_in.set_callback(on_message)
midi_in.open_port(port)

print(f"\nCapturing {ports[port]}. Play the pad now; press any key to stop.")
fd = sys.stdin.fileno()
old = termios.tcgetattr(fd)
try:
    tty.setcbreak(fd)
    last_count = -1
    while True:
        if select.select([sys.stdin], [], [], 0.25)[0]:
            sys.stdin.read(1)
            break
        if len(events) != last_count:
            last_count = len(events)
            print(f"\r  {last_count} messages", end="", flush=True)
finally:
    termios.tcsetattr(fd, termios.TCSADRAIN, old)
midi_in.close_port()
print()


def describe(msg):
    status = msg[0]
    kind, ch = status & 0xF0, (status & 0x0F) + 1
    if kind == 0xB0 and len(msg) >= 3:
        return f"CC ch {ch} cc {msg[1]} value {msg[2]}"
    if kind == 0x90 and len(msg) >= 3:
        return f"note-on ch {ch} note {msg[1]} velocity {msg[2]}"
    if kind == 0x80 and len(msg) >= 3:
        return f"note-off ch {ch} note {msg[1]} velocity {msg[2]}"
    if kind == 0xE0 and len(msg) >= 3:
        return f"pitch-bend ch {ch} value {msg[1] | msg[2] << 7}"
    if kind == 0xD0 and len(msg) >= 2:
        return f"pressure ch {ch} value {msg[1]}"
    return "other"


with open(out_path, "w") as f:
    f.write("# MIDI capture for Polar XY's Touch Filter\n")
    f.write(f"# device: {ports[port]}\n")
    f.write(f"# captured: {time.strftime('%Y-%m-%d %H:%M:%S')}, {len(events)} messages\n")
    f.write("# seconds since the first message, raw bytes (hex), what it is\n")
    for t, msg in events:
        f.write(f"{t:.6f}\t{' '.join(f'{b:02X}' for b in msg)}\t{describe(msg)}\n")

print(f"Saved {len(events)} messages to:\n  {out_path}")
print("Drop that file into the chat.")
PY
"$ENV/bin/python" "$SCRIPT" "$OUT" </dev/tty
