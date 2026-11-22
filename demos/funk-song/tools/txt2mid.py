"""Turns the funk song's note logs (lines of "seconds status data1 [data2]") into a Standard MIDI File.

    python3 txt2mid.py out.mid bpm drums.txt bass.txt epiano.txt lead.txt

Writes a type-1 file, 480 ticks a beat at the given tempo, one track per input file (named after it).
Any channel message is kept as is (note on/off, pitch bend, pressure, CC). No dependencies.
"""
import os, struct, sys

PPQ = 480
DATA_BYTES = {0x80: 2, 0x90: 2, 0xA0: 2, 0xB0: 2, 0xC0: 1, 0xD0: 1, 0xE0: 2}


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def track(events):
    body, last = b'', 0
    for tick, msg in events:
        body += vlq(tick - last) + msg
        last = tick
    body += vlq(0) + b'\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(body)) + body


def read(path, bpm):
    ev = []
    for i, line in enumerate(open(path)):
        p = line.split()
        if len(p) < 3:
            continue
        t, st = float(p[0]), int(p[1])
        n = DATA_BYTES.get(st & 0xF0)
        if n is None:
            continue
        data = [int(x) & 0x7F for x in p[2:2 + n]]
        tick = max(0, round(t * bpm / 60 * PPQ))
        # Note-offs before note-ons at the same tick, then file order.
        ev.append((tick, 0 if (st & 0xF0) == 0x80 or ((st & 0xF0) == 0x90 and data[1] == 0) else 1, i, bytes([st] + data)))
    ev.sort()
    return [(t, m) for t, _, _, m in ev]


def main():
    out, bpm, files = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
    tempo = round(60_000_000 / bpm)
    conductor = [(0, b'\xff\x51\x03' + tempo.to_bytes(3, 'big')), (0, b'\xff\x58\x04\x04\x02\x18\x08')]
    tracks = [track(conductor)]
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0].encode()
        tracks.append(track([(0, b'\xff\x03' + vlq(len(name)) + name)] + read(f, bpm)))
    with open(out, 'wb') as fh:
        fh.write(b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks), PPQ) + b''.join(tracks))


if __name__ == '__main__':
    main()
