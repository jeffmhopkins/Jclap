"""Finds where the MatrixBrute keeps its button settings and matrix routings in a preset.

The knobs are already decoded (mb_bank.py). The buttons, lights and matrix aren't on MIDI, so the
only way to find them is to change one on the synth, save the panel into the scratch slot, read the
slot back and see which byte moved. This walks through that, one change at a time:

    python mb_map.py OUTDIR [--bank 15 --slot 3]      (P04 by default)

For each step it says what to change on the synth's panel. Make the change, save the panel into the
scratch slot on the synth, then press Enter here: it reads the slot, saves it as OUTDIR/NN-name.syx
and prints the bytes that changed since the last step. Type s to skip a step, r to read again, or q
to stop. The changes add up (nothing is put back), so each step only differs from the one before.
Everything it prints also goes to OUTDIR/map.log.

READ ONLY, like mb_dump.py: it never sends a write or a setting. The saving is done on the synth.
Needs Python 3, mido and python-rtmidi, with MIDI Control Center and Bitwig closed (they hold the port).
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mb_bank  # noqa: E402
import mb_dump  # noqa: E402

# (file name, what to do on the synth). Each step changes one thing from the step before.
STEPS = [
    ("baseline", "Nothing to change: just save the panel as it is."),
    ("vco3-wave-1", "VCO 3 / LFO 3: press Wave once."),
    ("vco3-wave-2", "VCO 3 / LFO 3: press Wave once more."),
    ("vco3-div-1", "VCO 3 / LFO 3: press LFO Div once."),
    ("vco3-kbd", "VCO 3 / LFO 3: press Kbd Track once."),
    ("noise-1", "Noise: press Type once."),
    ("noise-2", "Noise: press Type once more."),
    ("sync", "VCO Sync: press VCO 2 > VCO 1 once."),
    ("wheel-1", "Wheels: press Mod Wheel once (its destination light moves)."),
    ("wheel-2", "Wheels: press Mod Wheel once more."),
    ("voice-1", "Voice: press Mode once."),
    ("voice-2", "Voice: press Mode once more."),
    ("mix-vco1-1", "Mixer: press VCO 1's Filter button once."),
    ("mix-vco1-2", "Mixer: press VCO 1's Filter button once more."),
    ("mix-vco2", "Mixer: press VCO 2's Filter button once."),
    ("mix-vco3", "Mixer: press VCO 3's Filter button once."),
    ("mix-noise", "Mixer: press Noise's Filter button once."),
    ("mix-ext", "Mixer: press External's Filter button once."),
    ("vcf1-mode-1", "VCF 1 Steiner: press Mode once."),
    ("vcf1-mode-2", "VCF 1 Steiner: press Mode once more."),
    ("vcf1-slope", "VCF 1 Steiner: press Slope once."),
    ("vcf2-mode-1", "VCF 2 Ladder: press Mode once."),
    ("vcf2-mode-2", "VCF 2 Ladder: press Mode once more."),
    ("vcf2-slope", "VCF 2 Ladder: press Slope once."),
    ("routing", "Filters: press Routing once."),
    ("lfo1-wave-1", "LFO 1: press Wave > once."),
    ("lfo1-wave-2", "LFO 1: press Wave > once more."),
    ("lfo1-sync", "LFO 1: press Seq-Sync once."),
    ("lfo1-retrig-1", "LFO 1: press Retrig once."),
    ("lfo1-retrig-2", "LFO 1: press Retrig once more."),
    ("lfo2-wave-1", "LFO 2: press Wave > once."),
    ("lfo2-sync", "LFO 2: press Seq-Sync once."),
    ("lfo2-retrig-1", "LFO 2: press Retrig once."),
    ("fx-mode-1", "Analog Effects: press Mode once."),
    ("fx-mode-2", "Analog Effects: press Mode once more."),
    ("fx-sync", "Analog Effects: press Sync once."),
    ("glide-on", "Glide: press Glide On/Off once."),
    ("priority-1", "Play Control: press Note Priority once."),
    ("priority-2", "Play Control: press Note Priority once more."),
    ("legato-1", "Play Control: press Legato once."),
    ("legato-2", "Play Control: press Legato once more."),
    ("keyhold", "Play Control: press Key Hold once."),
    ("seq-rate", "Sequencer: press the 1/8 button once."),
    ("seq-dir", "Sequencer: press Rev once."),
    ("matrix-a1", "Matrix: press MOD, press the cell in row A (ENV 1), column 1 (VCO 1 Pitch), and turn "
                  "Mod Amount up to about +50."),
    ("matrix-e9", "Matrix: press the cell in row E (LFO 1), column 9 (Steiner Cutoff), and turn Mod "
                  "Amount down to about -30."),
    ("matrix-p16", "Matrix: press the cell in row P (Exp 2 / M4), column 16, and turn Mod Amount up to "
                   "full."),
    ("matrix-a1-off", "Matrix: press the row A, column 1 cell again and turn its amount back to 0."),
]


class Log:
    def __init__(self, path):
        self.f = open(path, "a", encoding="utf-8")

    def __call__(self, text=""):
        print(text)
        self.f.write(text + "\n")
        self.f.flush()


def read_slot(syn, bank, slot):
    header = mb_dump.read_header(syn, bank, slot)
    data = mb_dump.read_data(syn, bank, slot)
    if not header or not data:
        return None
    return b"".join([header] + data)


def unpacked(raw):
    chunks = [m for m in mb_bank.messages(raw) if len(m) > 9 and m[7] == 0x20 and m[8] in (0x16, 0x17)]
    return mb_bank.unpack(b"".join(c[9:-1] for c in chunks))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--bank", type=int, default=15)
    ap.add_argument("--slot", type=int, default=3)
    ap.add_argument("--start", type=int, default=0, help="step to start at (to carry on after a stop)")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    log = Log(os.path.join(args.outdir, "map.log"))
    slot_name = "%s%02d" % ("ABCDEFGHIJKLMNOP"[args.bank], args.slot + 1)
    syn = mb_dump.Synth()
    log("Mapping into %s. Each time: make the change, save the panel into %s on the synth, then press "
        "Enter here." % (args.outdir, slot_name))
    previous = None
    if args.start > 0:
        prev_path = os.path.join(args.outdir, "%02d-%s.syx" % (args.start - 1, STEPS[args.start - 1][0]))
        if os.path.exists(prev_path):
            with open(prev_path, "rb") as f:
                previous = unpacked(f.read())
    for i in range(args.start, len(STEPS)):
        name, what = STEPS[i]
        log()
        log("Step %d of %d (%s): %s" % (i + 1, len(STEPS), name, what))
        while True:
            answer = input("  Saved to %s? Enter to read, s to skip, q to stop: " % slot_name).strip().lower()
            if answer == "q":
                log("Stopped at step %d. Carry on later with --start %d." % (i + 1, i))
                return
            if answer == "s":
                log("  skipped")
                break
            raw = read_slot(syn, args.bank, args.slot)
            if raw is None:
                log("  The read failed; check the cable and try again.")
                continue
            with open(os.path.join(args.outdir, "%02d-%s.syx" % (i, name)), "wb") as f:
                f.write(raw)
            current = unpacked(raw)
            if previous is not None:
                changed = [(o, previous[o], current[o]) for o in range(min(len(previous), len(current)))
                           if previous[o] != current[o]]
                if not changed:
                    log("  No byte changed. Was it saved? Type r to read again, or s to move on.")
                    again = input("  r or s: ").strip().lower()
                    if again == "r":
                        continue
                    log("  (no change)")
                for o, a, b in changed:
                    log("  byte %3d: %3d -> %3d" % (o, a, b))
            else:
                log("  read %d bytes" % len(current))
            previous = current
            break
    log()
    log("All done. Zip %s and send it over." % args.outdir)


if __name__ == "__main__":
    main()
