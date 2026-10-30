"""Follows the MatrixBrute presets controller script's log while Bitwig runs.

    python tools/mb_bitwig_log.py [read|stop] [--seconds N]

Connects to the script's log server (localhost:58017), prints every line it has logged since it
loaded, then follows new ones. "read" starts a preset read first, as the script's Read button does;
"stop" abandons one. Bitwig frames every message on that connection with a 4-byte big-endian length.
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
        sock.sendall(struct.pack(">I", len(word)) + word)
    sock.settimeout(0.5)
    end = time.time() + seconds if seconds else None
    buf = b""
    while end is None or time.time() < end:
        try:
            data = sock.recv(65536)
        except socket.timeout:
            continue
        if not data:
            print("(Bitwig closed the connection)")
            return
        buf += data
        while len(buf) >= 4:
            n = struct.unpack(">I", buf[:4])[0]
            if len(buf) < 4 + n:
                break
            sys.stdout.write(buf[4:4 + n].decode("ascii", "replace"))
            sys.stdout.flush()
            buf = buf[4 + n:]


if __name__ == "__main__":
    main()
