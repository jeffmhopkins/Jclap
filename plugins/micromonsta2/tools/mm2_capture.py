"""Captures MicroMonsta 2 program dumps over USB MIDI, so the plugin's factory bank can be decoded.

Needs Python 3 and `pip install mido python-rtmidi`. Run it with the synth plugged in by USB:

    python mm2_capture.py OUTDIR

It writes OUTDIR/bank.syx (UTILITY U02, every program), then sets every sound CC to a known pattern
and saves OUTDIR/cal-<pattern>.syx (UTILITY U01, the active program) for each, and finally checks
Bank Select / Program Change by loading program 106 and saving OUTDIR/pc-106.syx. Each step waits
for you to hold Yes on the synth. Nothing is saved on the synth: the CC edits are dropped when the
last step reloads program 1.
"""

import os
import sys
import time

import mido

# Every sound CC the plugin sends, in cc_map.h order.
CCS = [39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 74, 71, 88, 89, 90, 73, 75, 79, 72, 80,
       81, 82, 83, 84, 85, 86, 87, 36, 37, 76, 77, 78, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62,
       26, 27, 28, 29, 30, 31, 25, 5, 33, 34, 91, 35]

# Constant patterns give each byte's range, and the two ramps (every CC a different value) tell
# which byte a CC lands in.
PATTERNS = {
    "zero": lambda i: 0,
    "max": lambda i: 127,
    "mid": lambda i: 64,
    "ramp-up": lambda i: 5 + 2 * i,
    "ramp-down": lambda i: 122 - 2 * i,
    "quarter": lambda i: 32,
    "three-quarter": lambda i: 96,
}


def find_port(names):
    for n in names:
        if "monsta" in n.lower():
            return n
    sys.exit("No MicroMonsta port found. Ports: %s" % names)


def collect(inport, prompt, idle=3.0):
    """Waits for SysEx, then returns every SysEx message until `idle` seconds pass without one."""
    for _ in inport.iter_pending():
        pass
    print("\n>>> " + prompt, flush=True)
    data, last = [], None
    while True:
        for msg in inport.iter_pending():
            if msg.type == "sysex":
                data.append(bytes([0xF0] + list(msg.data) + [0xF7]))
                last = time.time()
        if last and time.time() - last > idle:
            return b"".join(data)
        time.sleep(0.01)


def save(path, data):
    with open(path, "wb") as f:
        f.write(data)
    print("    saved %s: %d bytes, %d messages" % (path, len(data), data.count(0xF0)), flush=True)


def program_change(out, ch, program):
    out.send(mido.Message("control_change", channel=ch, control=0, value=0))
    out.send(mido.Message("control_change", channel=ch, control=32, value=(program - 1) // 100))
    out.send(mido.Message("program_change", channel=ch, program=(program - 1) % 100))
    time.sleep(0.3)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "mm2-dumps"
    ch = int(sys.argv[2]) - 1 if len(sys.argv) > 2 else 0
    os.makedirs(outdir, exist_ok=True)
    inport = mido.open_input(find_port(mido.get_input_names()))
    out = mido.open_output(find_port(mido.get_output_names()))

    save(os.path.join(outdir, "bank.syx"),
         collect(inport, "On the synth: UTILITY > U02 (send all programs), hold Yes.", idle=5.0))

    program_change(out, ch, 1)
    for name, value in PATTERNS.items():
        for i, cc in enumerate(CCS):
            out.send(mido.Message("control_change", channel=ch, control=cc, value=value(i)))
            time.sleep(0.005)
        time.sleep(0.3)
        save(os.path.join(outdir, "cal-%s.syx" % name),
             collect(inport, "Pattern '%s' sent. On the synth: UTILITY > U01 (send program), hold Yes." % name))

    program_change(out, ch, 106)
    save(os.path.join(outdir, "pc-106.syx"),
         collect(inport, "Sent Bank 1 / Program 5. On the synth: UTILITY > U01, hold Yes."))
    program_change(out, ch, 1)
    print("\nDone. Program 1 reloaded; nothing was saved on the synth.")


if __name__ == "__main__":
    main()
