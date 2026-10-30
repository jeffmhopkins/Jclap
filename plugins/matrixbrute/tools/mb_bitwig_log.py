"""Follows the MatrixBrute presets controller script's log while Bitwig runs.

    python tools/mb_bitwig_log.py [read|stop] [--data] [--seconds N]

Subscribes to the script over OSC (UDP, localhost:58017; see bitwig/MatrixBrute.control.js), prints
every line it has logged since it loaded, then follows new ones. "read" starts a preset read, as the
script's Read button does; "stop" abandons one. --data shows what the plugin gets instead of the log.
"""

import socket
import struct
import sys
import time

PORT = 58017
# The script can only answer these ports (it opens its connections as it starts).
CLIENT_PORTS = range(58018, 58026)


def bind_client(sock):
    for p in CLIENT_PORTS:
        try:
            sock.bind(("127.0.0.1", p))
            return p
        except OSError:
            continue
    sys.exit("Ports 58018-58025 are all taken")


def pad(b):
    return b + b"\0" * (4 - len(b) % 4)


def osc(address, *args):
    tags = "," + "".join("i" if isinstance(a, int) else "s" for a in args)
    out = pad(address.encode()) + pad(tags.encode())
    for a in args:
        out += struct.pack(">i", a) if isinstance(a, int) else pad(a.encode())
    return out


def string_at(data, i):
    end = data.index(b"\0", i)
    return data[i:end].decode("ascii", "replace"), (end + 4) & ~3


def parse(data):
    """(address, [args]) for a message of strings and ints."""
    address, i = string_at(data, 0)
    tags, i = string_at(data, i)
    args = []
    for t in tags[1:]:
        if t == "i":
            args.append(struct.unpack(">i", data[i:i + 4])[0])
            i += 4
        elif t == "s":
            s, i = string_at(data, i)
            args.append(s)
    return address, args


def main():
    args = sys.argv[1:]
    seconds = None
    if "--seconds" in args:
        i = args.index("--seconds")
        seconds = float(args[i + 1])
        del args[i:i + 2]
    kind = "data" if "--data" in args else "log"
    args = [a for a in args if a != "--data"]
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    port = bind_client(sock)
    sock.settimeout(0.5)
    script = ("127.0.0.1", PORT)
    sock.sendto(osc("/mb/hello", port, kind), script)
    if args:
        sock.sendto(osc("/mb/" + args[0]), script)
    end = time.time() + seconds if seconds else None
    last_hello, answered = time.time(), False
    while end is None or time.time() < end:
        if time.time() - last_hello > 3:
            sock.sendto(osc("/mb/hello", port, kind), script)
            last_hello = time.time()
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            continue
        except ConnectionResetError:
            # Windows reports a port nobody listens on (Bitwig closed) this way.
            continue
        address, values = parse(data)
        if address == "/mb/hello":
            if not answered:
                print("(connected to the script)", file=sys.stderr)
                answered = True
        elif address == "/mb/line" and values:
            print(values[0], flush=True)


if __name__ == "__main__":
    main()
