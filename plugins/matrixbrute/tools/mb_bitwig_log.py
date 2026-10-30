"""Follows the MatrixBrute presets controller script's log while Bitwig runs.

    python tools/mb_bitwig_log.py [read|stop] [--seconds N]

Connects to the script's log server (localhost:58017), prints every line it has logged since it
loaded, then follows new ones. "read" starts a preset read first, as the script's Read button does;
"stop" abandons one. What a client sends needs a 4-byte little-endian length in front; what the script
sends comes as plain lines (checked with Bitwig 5 on Windows).
"""

import socket
import struct
import sys
import time

PORT = 58017


def main():
    args = sys.argv[1:]
    seconds = None
    if "--seconds" in args:
        i = args.index("--seconds")
        seconds = float(args[i + 1])
        del args[i:i + 2]
    sock = socket.create_connection(("127.0.0.1", PORT), timeout=5)
    if args:
        word = args[0].encode()
        sock.sendall(struct.pack("<I", len(word)) + word)
    sock.settimeout(0.5)
    end = time.time() + seconds if seconds else None
    while end is None or time.time() < end:
        try:
            data = sock.recv(65536)
        except socket.timeout:
            continue
        if not data:
            print("(Bitwig closed the connection)")
            return
        sys.stdout.write(data.decode("ascii", "replace"))
        sys.stdout.flush()


if __name__ == "__main__":
    main()
