"""Calibration rounds for decoding the MatrixBrute's preset format.

Each round sets every sound CC to a known pattern, the user saves the panel into a scratch slot on
the synth, and the slot is read back. With a few rounds, each byte of the preset that follows a knob
shows up as a function of that knob's value.

    python mb_calibrate.py send ROUND            set the CCs for round 0-3 (on all 16 channels)
    python mb_calibrate.py read ROUND OUTDIR [--bank 15 --slot 3]
                                                 read the scratch slot into OUTDIR/calib-ROUND.syx

`send` only sends CCs. `read` only reads (see mb_dump.py). Neither writes a preset: the user saves
on the synth. Needs Python 3, mido and python-rtmidi, and mb_dump.py beside this file.
"""

import argparse
import os
import sys
import time

import mido

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mb_dump  # noqa: E402

# Every CC in src/cc_map.h, in its order, except the mod wheel (1), which isn't part of a preset.
CCS = [9, 10, 12, 13, 66, 65, 68, 69, 70, 67, 14, 15, 16, 17, 72, 71, 74, 75, 76, 73, 22, 77, 78, 79, 80,
       18, 19, 20, 21, 89, 23, 83, 82, 81, 24, 7, 25, 87, 86, 85, 26, 8, 27, 102, 103, 28, 104, 94, 105,
       106, 29, 107, 95, 108, 109, 110, 30, 111, 91, 90, 93, 92, 31, 112, 113, 114, 115, 116, 117, 118,
       5, 119, 11, 2, 4, 3]


def pattern(round_):
    """{cc: value}. Within a round every CC gets a different value, so a byte that follows one knob
    names it; across rounds the values differ, so the byte's scaling shows."""
    n = len(CCS)
    if round_ == 0:
        values = [(5 * i + 3) % 128 for i in range(n)]
    elif round_ == 1:
        values = [(37 * i + 11) % 128 for i in range(n)]
    elif round_ == 2:
        values = [127 - (5 * i + 3) % 128 for i in range(n)]
    elif round_ == 3:
        values = [(77 * i + 50) % 128 for i in range(n)]
    else:
        sys.exit("round is 0-3")
    return dict(zip(CCS, values))


def send(round_):
    out = mido.open_output(mb_dump.find_port(mido.get_output_names()))
    values = pattern(round_)
    for ch in range(16):
        for cc, v in values.items():
            out.send(mido.Message("control_change", channel=ch, control=cc, value=v))
            time.sleep(0.001)
    out.close()
    print("Round %d sent: %d CCs on all channels." % (round_, len(values)))


def read(round_, outdir, bank, slot):
    os.makedirs(outdir, exist_ok=True)
    syn = mb_dump.Synth()
    header = mb_dump.read_header(syn, bank, slot)
    data = mb_dump.read_data(syn, bank, slot)
    if not header or not data:
        sys.exit("Read failed")
    path = os.path.join(outdir, "calib-%d.syx" % round_)
    with open(path, "wb") as f:
        f.write(b"".join([header] + data))
    print("Read bank %d slot %d (%d chunks) into %s" % (bank, slot, len(data) - 1, path))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["send", "read"])
    ap.add_argument("round", type=int)
    ap.add_argument("outdir", nargs="?")
    ap.add_argument("--bank", type=int, default=15)
    ap.add_argument("--slot", type=int, default=3)
    args = ap.parse_args()
    if args.action == "send":
        send(args.round)
    else:
        if not args.outdir:
            sys.exit("read needs OUTDIR")
        read(args.round, args.outdir, args.bank, args.slot)


if __name__ == "__main__":
    main()
