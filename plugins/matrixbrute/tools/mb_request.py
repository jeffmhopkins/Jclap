"""Sends SysEx to the MatrixBrute and saves what it answers, to check a request found in a capture.

Needs Python 3 and `pip install mido python-rtmidi`. With the synth on USB and MIDI Control Center
closed (it holds the port):

    python mb_request.py REQUEST.syx REPLY.syx [--idle 2]

REQUEST.syx holds one or more F0 ... F7 messages, for example lines picked from the "Distinct SysEx
sent to the synth" list in mb_usb_extract.py's summary.txt (`--hex "F0 00 20 6B ... F7"` takes one
typed in instead). Each is sent in turn, and every SysEx the synth sends back is saved to REPLY.syx
once it has been quiet for --idle seconds. If the replies match what MCC got, the plugin can ask for
presets the same way, with no front panel steps.
"""

import argparse
import sys
import time

import mido


def find_port(names):
    for n in names:
        if "matrixbrute" in n.lower().replace(" ", ""):
            return n
    sys.exit("No MatrixBrute port found. Ports: %s" % names)


def messages(data):
    out, start = [], None
    for i, b in enumerate(data):
        if b == 0xF0:
            start = i
        elif b == 0xF7 and start is not None:
            out.append(data[start:i + 1])
            start = None
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("request", nargs="?", help=".syx file with the message(s) to send")
    ap.add_argument("reply", help=".syx file to save the replies to")
    ap.add_argument("--hex", help="one message as hex bytes instead of a file")
    ap.add_argument("--idle", type=float, default=2.0, help="seconds of quiet that end the reply")
    ap.add_argument("--gap", type=float, default=0.05, help="seconds between requests")
    args = ap.parse_args()

    if args.hex:
        data = bytes(int(x, 16) for x in args.hex.split())
    elif args.request:
        with open(args.request, "rb") as f:
            data = f.read()
    else:
        sys.exit("Give a request .syx file or --hex.")
    requests = messages(data)
    if not requests:
        sys.exit("No F0 ... F7 message in the request.")

    inport = mido.open_input(find_port(mido.get_input_names()))
    out = mido.open_output(find_port(mido.get_output_names()))
    for _ in inport.iter_pending():
        pass

    replies, last = [], time.time()
    for r in requests:
        out.send(mido.Message("sysex", data=list(r[1:-1])))
        time.sleep(args.gap)
        for msg in inport.iter_pending():
            if msg.type == "sysex":
                replies.append(bytes([0xF0] + list(msg.data) + [0xF7]))
                last = time.time()
    while time.time() - last < args.idle:
        for msg in inport.iter_pending():
            if msg.type == "sysex":
                replies.append(bytes([0xF0] + list(msg.data) + [0xF7]))
                last = time.time()
        time.sleep(0.01)

    with open(args.reply, "wb") as f:
        f.write(b"".join(replies))
    print("Sent %d message(s); got %d SysEx back (%d bytes), saved to %s" % (
        len(requests), len(replies), sum(map(len, replies)), args.reply))


if __name__ == "__main__":
    main()
