"""Checks mb_usb_extract.py on made-up USBPcap captures: SysEx split over USB MIDI packets and over
several transfers comes back whole, each way, from the right device, in pcap and pcapng files."""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "mb_usb_extract.py")

REQUEST = bytes([0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x01, 0x00, 0xF7])
REPLY = bytes([0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02]) + bytes(range(40)) + bytes([0xF7])
CC = bytes([0xB0, 23, 99])


def usb_midi(msg):
    """USB MIDI 1.0 packets for one message on cable 0."""
    if msg[0] != 0xF0:
        return bytes([msg[0] >> 4]) + msg + bytes(3 - len(msg))
    out = b""
    for i in range(0, len(msg), 3):
        chunk = msg[i:i + 3]
        if i + 3 < len(msg):
            out += bytes([0x4]) + chunk
        else:
            out += bytes([0x4 + len(chunk)]) + chunk + bytes(3 - len(chunk))
    return out


def record(device, endpoint, completion, data, kind=3):
    header = struct.pack("<HQIHBHHBBI", 27, 1, 0, 9, 1 if completion else 0, 1, device, endpoint, kind, len(data))
    return header + data


def packets():
    reply = usb_midi(REPLY)
    return [
        # Another device's MIDI-shaped traffic must be ignored.
        record(7, 0x02, False, usb_midi(bytes([0x90, 60, 100]))),
        record(5, 0x02, False, usb_midi(REQUEST)),
        record(5, 0x02, True, b""),  # the OUT completion carries no data
        # The reply arrives in two IN transfers, cut mid-SysEx.
        record(5, 0x81, True, reply[:16]),
        record(5, 0x81, True, reply[16:] + usb_midi(CC)),
        record(5, 0x81, False, b""),  # the IN request carries no data
    ]


def pcap(records):
    out = struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 249)
    for i, r in enumerate(records):
        out += struct.pack("<IIII", 100, i * 1000, len(r), len(r)) + r
    return out


def pcapng(records):
    def block(btype, body):
        body += bytes((4 - len(body) % 4) % 4)
        n = len(body) + 12
        return struct.pack("<II", btype, n) + body + struct.pack("<I", n)

    out = block(0x0A0D0D0A, struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1))
    out += block(1, struct.pack("<HHI", 249, 0, 65535))
    for i, r in enumerate(records):
        out += block(6, struct.pack("<IIIII", 0, 0, i * 1000, len(r), len(r)) + r)
    return out


def check(name, data):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, name)
        with open(path, "wb") as f:
            f.write(data)
        outdir = os.path.join(tmp, "out")
        result = subprocess.run([sys.executable, "-I", TOOL, path, outdir, "--device", "1.5"],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        with open(os.path.join(outdir, "to-synth.syx"), "rb") as f:
            assert f.read() == REQUEST, name + ": request"
        with open(os.path.join(outdir, "from-synth.syx"), "rb") as f:
            assert f.read() == REPLY, name + ": reply"
        with open(os.path.join(outdir, "messages.txt")) as f:
            text = f.read()
        assert "cc ch1" in text and "B0 17 63" in text, name + ": the CC after the reply"
        assert "note on" not in text, name + ": another device's traffic"

        # Without --device it finds the one carrying Arturia SysEx.
        result = subprocess.run([sys.executable, "-I", TOOL, path, outdir], capture_output=True, text=True)
        assert result.returncode == 0 and "Using device 1.5" in result.stdout, result.stdout + result.stderr


def main():
    check("capture.pcap", pcap(packets()))
    check("capture.pcapng", pcapng(packets()))
    print("ok")


if __name__ == "__main__":
    main()
