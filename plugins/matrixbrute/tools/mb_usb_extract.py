"""Pulls the MIDI out of a USB capture of the MatrixBrute, so its SysEx can be decoded.

Arturia doesn't document how MIDI Control Center reads presets from the synth. This reads a capture of
the USB cable taken while MCC talks to it (USBPcap on Windows, via Wireshark's dumpcap or USBPcapCMD,
see ../docs/capture.md) and turns it back into MIDI. No extra Python packages needed:

    python mb_usb_extract.py capture.pcapng OUTDIR [--device BUS.DEV]

It writes to OUTDIR:

- to-synth.syx and from-synth.syx: every SysEx message each way, in order;
- messages.txt: a timeline of every MIDI message, with its time, direction and bytes;
- summary.txt: the distinct messages sent to the synth (the requests MCC uses), the replies'
  lengths, and the bytes every reply shares.

Without --device it picks the device whose traffic carries Arturia SysEx (manufacturer 00 20 6B), or
whose device descriptor has Arturia's USB vendor id 0x1C75.
"""

import argparse
import collections
import os
import struct
import sys

ARTURIA_SYSEX = bytes([0xF0, 0x00, 0x20, 0x6B])
ARTURIA_VID = 0x1C75
LINKTYPE_USBPCAP = 249

# USB MIDI 1.0 event packets: the Code Index Number (low nibble of byte 0) says how many of the
# three MIDI bytes after it count. 0x4 is SysEx going on, 0x5-0x7 SysEx (or a 1-3 byte system
# message) ending with 1-3 bytes.
CIN_LENGTH = {0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3, 0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3,
              0xC: 2, 0xD: 2, 0xE: 3, 0xF: 1}


def read_packets(path):
    """Yields (seconds, link type, packet bytes) from a pcap or pcapng file."""
    with open(path, "rb") as f:
        data = f.read()
    magic = data[:4]
    if magic == b"\x0a\x0d\x0d\x0a":
        yield from _pcapng(data)
    elif magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1", b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
        yield from _pcap(data)
    else:
        sys.exit("%s is not a pcap or pcapng file" % path)


def _pcap(data):
    little = data[:4] in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1")
    nano = data[:4] in (b"\x4d\x3c\xb2\xa1", b"\xa1\xb2\x3c\x4d")
    e = "<" if little else ">"
    link = struct.unpack_from(e + "I", data, 20)[0]
    pos = 24
    while pos + 16 <= len(data):
        sec, frac, caplen, _ = struct.unpack_from(e + "IIII", data, pos)
        pos += 16
        yield sec + frac / (1e9 if nano else 1e6), link, data[pos:pos + caplen]
        pos += caplen


def _pcapng(data):
    e = "<"
    links, resolutions = [], []
    pos = 0
    while pos + 12 <= len(data):
        btype, blen = struct.unpack_from(e + "II", data, pos)
        if btype == 0x0A0D0D0A:
            # The byte order magic decides the order of everything in this section.
            e = "<" if data[pos + 8:pos + 12] == b"\x4d\x3c\x2b\x1a" else ">"
            btype, blen = struct.unpack_from(e + "II", data, pos)
            links, resolutions = [], []
        body = data[pos + 8:pos + blen - 4]
        if btype == 1:  # interface description
            links.append(struct.unpack_from(e + "H", body, 0)[0])
            resolutions.append(_ts_resolution(body[8:], e))
        elif btype == 6:  # enhanced packet
            iface, hi, lo, caplen, _ = struct.unpack_from(e + "IIIII", body, 0)
            ts = ((hi << 32) | lo) / resolutions[iface] if iface < len(resolutions) else 0.0
            yield ts, links[iface] if iface < len(links) else None, body[20:20 + caplen]
        elif btype == 3:  # simple packet
            yield 0.0, links[0] if links else None, body[4:]
        if blen < 12:
            break
        pos += blen


def _ts_resolution(options, e):
    pos = 0
    while pos + 4 <= len(options):
        code, length = struct.unpack_from(e + "HH", options, pos)
        if code == 0:
            break
        if code == 9 and length >= 1:  # if_tsresol
            v = options[pos + 4]
            return float(2 ** (v & 0x7F)) if v & 0x80 else float(10 ** v)
        pos += 4 + ((length + 3) & ~3)
    return 1e6


class Transfer:
    """One USBPcap record: which device and endpoint, which way, and its data."""

    def __init__(self, ts, raw):
        (hlen, self.irp, self.status, self.function, info, self.bus, self.device, self.endpoint,
         self.kind, dlen) = struct.unpack_from("<HQIHBHHBBI", raw, 0)
        self.ts = ts
        self.completion = bool(info & 1)  # device to host side of the request
        self.data = raw[hlen:hlen + dlen]

    @property
    def to_host(self):
        return bool(self.endpoint & 0x80)

    @property
    def key(self):
        return "%d.%d" % (self.bus, self.device)


def transfers(path):
    for ts, link, raw in read_packets(path):
        if link == LINKTYPE_USBPCAP and len(raw) >= 27:
            yield Transfer(ts, raw)


def midi_payload(t):
    """The bulk or interrupt data that carries MIDI, or None. Data going out is in the request,
    data coming in is in its completion."""
    if t.kind not in (1, 3) or not t.data:
        return None
    if t.to_host != t.completion:
        return None
    return t.data


def looks_like_midi(data):
    if len(data) % 4:
        return False
    for i in range(0, len(data), 4):
        if data[i:i + 4] != b"\0\0\0\0" and (data[i] & 0x0F) not in CIN_LENGTH:
            return False
    return True


def vendor_ids(all_transfers):
    """Device key -> USB vendor id, from any device descriptor seen (USBPcap's --inject-descriptors
    or a plug-in during the capture)."""
    found = {}
    for t in all_transfers:
        d = t.data
        if t.kind == 2 and len(d) >= 18 and d[0] == 18 and d[1] == 1:
            found[t.key] = struct.unpack_from("<H", d, 8)[0]
    return found


def pick_device(all_transfers, wanted):
    if wanted:
        return wanted
    vids = vendor_ids(all_transfers)
    score, count = collections.Counter(), collections.Counter()
    for t in all_transfers:
        data = midi_payload(t)
        if data is not None and looks_like_midi(data):
            score[t.key] += 1
            count[t.key] += 1
            midi = b"".join(data[i + 1:i + 4] for i in range(0, len(data), 4))
            if ARTURIA_SYSEX in midi:
                score[t.key] += 1000
    for key, vid in vids.items():
        if vid == ARTURIA_VID:
            score[key] += 100000
    if not score:
        sys.exit("No USB MIDI traffic found. Was the capture on the hub the MatrixBrute is on?")
    best, _ = score.most_common(1)[0]
    print("Devices with MIDI-shaped traffic: %s" % ", ".join(
        "%s%s (%d transfers)" % (k, " [Arturia]" if vids.get(k) == ARTURIA_VID else "", count[k])
        for k, _ in score.most_common()))
    print("Using device %s (pass --device to pick another)" % best)
    return best


class Decoder:
    """Turns one direction's USB MIDI packets back into MIDI messages."""

    def __init__(self):
        self.sysex = {}  # cable -> bytes so far

    def feed(self, data):
        out = []
        for i in range(0, len(data) - 3, 4):
            cable, cin = data[i] >> 4, data[i] & 0x0F
            body = data[i + 1:i + 1 + CIN_LENGTH.get(cin, 0)]
            if not body:
                continue
            if cin == 0x4 or (cin in (0x5, 0x6, 0x7) and (cable in self.sysex or body[0] == 0xF0)):
                buf = self.sysex.setdefault(cable, bytearray())
                buf += body
                if cin != 0x4:
                    out.append(bytes(self.sysex.pop(cable)))
            else:
                out.append(bytes(body))
        return out


def describe(msg):
    status = msg[0]
    if status == 0xF0:
        return "sysex %d bytes" % len(msg)
    kind = {0x80: "note off", 0x90: "note on", 0xA0: "poly at", 0xB0: "cc", 0xC0: "program",
            0xD0: "chan at", 0xE0: "bend"}.get(status & 0xF0)
    if kind:
        return "%s ch%d" % (kind, (status & 0x0F) + 1)
    return "system %02X" % status


def hexs(b, limit=None):
    text = " ".join("%02X" % x for x in (b if limit is None else b[:limit]))
    return text + (" ... (%d bytes)" % len(b) if limit is not None and len(b) > limit else "")


def common_prefix(messages):
    if not messages:
        return b""
    first = messages[0]
    n = len(first)
    for m in messages[1:]:
        n = min(n, len(m))
        for i in range(n):
            if m[i] != first[i]:
                n = i
                break
    return first[:n]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", nargs="+", help="pcap or pcapng file(s) from USBPcap")
    ap.add_argument("outdir")
    ap.add_argument("--device", help="bus.device to use, as listed when it runs")
    ap.add_argument("--all-messages", action="store_true", help="also list clock and active sensing")
    args = ap.parse_args()

    all_transfers = [t for path in args.capture for t in transfers(path)]
    if not all_transfers:
        sys.exit("No USBPcap packets in %s" % ", ".join(args.capture))
    all_transfers.sort(key=lambda t: t.ts)
    device = pick_device(all_transfers, args.device)

    decoders = {True: Decoder(), False: Decoder()}
    timeline = []  # (seconds, to_host, message)
    for t in all_transfers:
        if t.key != device:
            continue
        data = midi_payload(t)
        if data is None or not looks_like_midi(data):
            continue
        for msg in decoders[t.to_host].feed(data):
            timeline.append((t.ts, t.to_host, msg))
    if not timeline:
        sys.exit("Device %s sent or got no MIDI in this capture." % device)

    os.makedirs(args.outdir, exist_ok=True)
    start = timeline[0][0]
    sent = [m for _, up, m in timeline if not up and m[0] == 0xF0]
    got = [m for _, up, m in timeline if up and m[0] == 0xF0]
    with open(os.path.join(args.outdir, "to-synth.syx"), "wb") as f:
        f.write(b"".join(sent))
    with open(os.path.join(args.outdir, "from-synth.syx"), "wb") as f:
        f.write(b"".join(got))
    with open(os.path.join(args.outdir, "messages.txt"), "w") as f:
        for ts, up, msg in timeline:
            if not args.all_messages and msg[0] in (0xF8, 0xFE):
                continue
            f.write("%9.3f %s %-16s %s\n" % (ts - start, "<- synth" if up else "-> synth", describe(msg), hexs(msg, 64)))

    lines = ["Device %s: %d messages to the synth, %d from it." % (
        device, sum(1 for _, up, _ in timeline if not up), sum(1 for _, up, _ in timeline if up)),
        "SysEx: %d sent (%d bytes), %d received (%d bytes)." % (
        len(sent), sum(map(len, sent)), len(got), sum(map(len, got))), ""]
    lines.append("Distinct SysEx sent to the synth (count, bytes):")
    for msg, n in collections.Counter(sent).most_common():
        lines.append("  %4d x  %s" % (n, hexs(msg, 48)))
    lines += ["", "SysEx replies by length (count, length, first bytes):"]
    by_len = collections.defaultdict(list)
    for m in got:
        by_len[len(m)].append(m)
    for length in sorted(by_len, key=lambda k: -len(by_len[k])):
        group = by_len[length]
        lines.append("  %4d x %5d bytes, shared start: %s" % (len(group), length, hexs(common_prefix(group), 24)))
    arturia = sum(1 for m in sent + got if m.startswith(ARTURIA_SYSEX))
    lines += ["", "%d of the SysEx messages carry Arturia's manufacturer id (00 20 6B)." % arturia]
    with open(os.path.join(args.outdir, "summary.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print("\nWrote to-synth.syx, from-synth.syx, messages.txt and summary.txt to %s" % args.outdir)


if __name__ == "__main__":
    main()
