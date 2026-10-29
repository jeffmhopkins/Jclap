"""Reads every preset off a MatrixBrute the way MIDI Control Center does, and saves the raw replies.

Needs Python 3 and `pip install mido python-rtmidi`. With the synth on USB and MIDI Control Center
closed (it holds the port):

    python mb_dump.py OUTDIR [--banks 0-15]

READ ONLY: it sends the identity request, the preset header/name read (03 19 bb ss 00), the preset
data read (03 19 bb ss 01) and the chunk poll (01 18 00). It never sends a write or a setting.

The protocol, from a USB capture of MCC (see ../docs/capture.md):

    F0 00 20 6B 06 01 <seq> <len> <cmd...> F7      seq counts 00-7F and wraps; the synth echoes it

    03 19 bb ss 00   ->  23 52 bb ss 00 <9 header bytes> <23-byte name, 00-padded>
    03 19 bb ss 01   ->  00 15                       ready; then, same seq:
    01 18 00         ->  20 16 <32 bytes>            one chunk; poll again with seq+1 ...
                     ->  20 17 <32 bytes>            ... until the last chunk

OUTDIR gets bBB-sSS.syx per preset (header reply, the 00 15, then every chunk, as received) and
names.tsv (bank, slot, header bytes, name). Slots that fail twice are listed in failed.txt.
"""

import argparse
import os
import sys
import time

import mido

ARTURIA = [0x00, 0x20, 0x6B, 0x06, 0x01]
TIMEOUT = 1.0


def find_port(names):
    for n in names:
        if n.lower().startswith("matrixbrute"):
            return n
    sys.exit("No MatrixBrute port found (is MIDI Control Center still open?). Ports: %s" % names)


class Synth:
    def __init__(self):
        self.inp = mido.open_input(find_port(mido.get_input_names()))
        self.out = mido.open_output(find_port(mido.get_output_names()))
        self.seq = 0
        self.drain()

    def drain(self, quiet=0.2):
        end = time.time() + quiet
        while time.time() < end:
            if any(True for _ in self.inp.iter_pending()):
                end = time.time() + quiet
            time.sleep(0.005)

    def send(self, data):
        self.out.send(mido.Message("sysex", data=data))

    def wait(self, test, timeout=TIMEOUT):
        """The first SysEx reply (as F0..F7 bytes) for which test(reply) holds, or None."""
        end = time.time() + timeout
        while time.time() < end:
            for msg in self.inp.iter_pending():
                if msg.type == "sysex":
                    raw = bytes([0xF0] + list(msg.data) + [0xF7])
                    if test(raw):
                        return raw
            time.sleep(0.0005)
        return None

    def request(self, cmd, seq=None, expect=None):
        """Sends an Arturia command (cmd byte, then its arguments) with a seq and the length byte (the
        number of argument bytes); returns the reply with that seq."""
        if seq is None:
            seq = self.next_seq()
        self.send(ARTURIA + [seq, len(cmd) - 1] + cmd)
        return self.wait(lambda r: r[1:6] == bytes(ARTURIA) and r[6] == seq and
                         (expect is None or r[8:8 + len(expect)] == bytes(expect)))

    def next_seq(self):
        s = self.seq
        self.seq = (self.seq + 1) & 0x7F
        return s


def read_header(syn, bank, slot):
    for _ in range(2):
        r = syn.request([0x19, bank, slot, 0x00], expect=[0x52, bank, slot])
        if r:
            return r
    return None


def read_data(syn, bank, slot):
    """[00 15 reply, chunk replies...] for one preset, or None."""
    seq = syn.next_seq()
    ready = syn.request([0x19, bank, slot, 0x01], seq=seq, expect=[0x15])
    if not ready:
        return None
    replies = [ready]
    while True:
        chunk = syn.request([0x18, 0x00], seq=seq)
        if not chunk or chunk[8] not in (0x16, 0x17):
            return None
        replies.append(chunk)
        seq = syn.next_seq()
        if chunk[8] == 0x17:
            # MCC sends one more poll after the last chunk; the synth doesn't answer it.
            syn.send(ARTURIA + [seq, 0x01, 0x18, 0x00])
            syn.wait(lambda r: False, 0.01)
            return replies
        if len(replies) > 200:
            return None


def parse_banks(text):
    lo, _, hi = text.partition("-")
    return range(int(lo), int(hi or lo) + 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--banks", default="0-15", help="bank range, 0-15")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    syn = Synth()
    syn.send([0x7E, 0x7F, 0x06, 0x01])
    ident = syn.wait(lambda r: r[1:5] == bytes([0x7E, 0x01, 0x06, 0x02]))
    print("Identity:", ident.hex(" ").upper() if ident else "no reply")

    banks = parse_banks(args.banks)
    headers, failed = {}, []
    for b in banks:
        for s in range(16):
            r = read_header(syn, b, s)
            if r:
                headers[(b, s)] = r
            else:
                failed.append((b, s, "header"))
    with open(os.path.join(args.outdir, "names.tsv"), "w", encoding="utf-8") as f:
        f.write("bank\tslot\theader\tname\n")
        for (b, s), r in sorted(headers.items()):
            header, name = r[12:21], r[21:-1]
            f.write("%d\t%d\t%s\t%s\n" % (b, s, header.hex(" ").upper(),
                                          name.split(b"\0")[0].decode("ascii", "replace")))
    print("Read %d headers" % len(headers))

    sizes = {}
    for b in banks:
        for s in range(16):
            data = None
            for attempt in range(2):
                data = read_data(syn, b, s)
                if data:
                    break
                syn.drain(0.5)
            if not data:
                failed.append((b, s, "data"))
                print("bank %d slot %d: FAILED" % (b, s))
                continue
            with open(os.path.join(args.outdir, "b%02d-s%02d.syx" % (b, s)), "wb") as f:
                f.write(b"".join(([headers[(b, s)]] if (b, s) in headers else []) + data))
            chunks = len(data) - 1
            sizes[chunks] = sizes.get(chunks, 0) + 1
            print("bank %d slot %d: %d chunks  %s" % (
                b, s, chunks, headers.get((b, s), b"")[21:-1].split(b"\0")[0].decode("ascii", "replace")))

    with open(os.path.join(args.outdir, "failed.txt"), "w") as f:
        for b, s, what in failed:
            f.write("%d\t%d\t%s\n" % (b, s, what))
    print("Done. Chunk counts: %s. Failures: %d" % (sizes, len(failed)))


if __name__ == "__main__":
    main()
