#!/usr/bin/env python3
"""Receive CPC TCP Protocol V1.0 frames from the Raspberry Pi CPC server."""

import argparse
import socket
import sys


HOST = "192.168.50.2"
PORT = 5000


def parse_frame(line):
    fields = line.split(",")
    if len(fields) != 5:
        raise ValueError("field count is not 5")
    if fields[0] != "$CPC":
        raise ValueError("frame header is not $CPC")
    if fields[1] != "1":
        raise ValueError("unsupported protocol version {}".format(fields[1]))

    sequence = int(fields[2])
    concentration = float(fields[3])
    status = int(fields[4])
    return sequence, concentration, status


def main():
    parser = argparse.ArgumentParser(
        description="Test client for CPC TCP Protocol V1.0."
    )
    parser.add_argument("--host", default=HOST, help="CPC server host")
    parser.add_argument("--port", default=PORT, type=int, help="CPC server port")
    args = parser.parse_args()

    receive_buffer = b""
    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        sock.settimeout(None)
        print("Connected to {}:{}".format(args.host, args.port))
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                print("Connection closed by CPC server")
                return 0

            receive_buffer += chunk
            while b"\r\n" in receive_buffer:
                frame, receive_buffer = receive_buffer.split(b"\r\n", 1)
                if not frame:
                    continue
                try:
                    line = frame.decode("ascii")
                    sequence, concentration, status = parse_frame(line)
                except (UnicodeDecodeError, ValueError) as exc:
                    print("Invalid frame {!r}: {}".format(frame, exc), file=sys.stderr)
                    continue

                print(
                    "Seq={}  Value={:.3f}  Status={}".format(
                        sequence, concentration, status
                    )
                )


if __name__ == "__main__":
    sys.exit(main())
