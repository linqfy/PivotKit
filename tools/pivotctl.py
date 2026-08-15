#!/usr/bin/env python3
"""pivotctl.py — send one command to the PivotKit bridge.

PivotKit starts a loopback TCP server on 127.0.0.1:50077 (enabled by default)
when pivotkit-loader.exe launches pivot.exe. This script connects, sends a
command line, and prints the reply.

Commands are the same as the console: registered pivotlib commands first,
then Lua expressions.

Usage:
    python pivotctl.py "pivotlib.frame()"
    python pivotctl.py "hud"                 # a registered command
    python pivotctl.py --port 50077 "1 + 1"

Exit code 0 on success, 1 if the connection/command failed.
"""
import argparse
import socket
import sys


def main():
    ap = argparse.ArgumentParser(description="Talk to the PivotKit bridge")
    ap.add_argument("--port", type=int, default=50077, help="bridge port")
    ap.add_argument("cmd", help="command line to execute inside Pivot")
    args = ap.parse_args()

    try:
        s = socket.create_connection(("127.0.0.1", args.port), timeout=10)
    except OSError as exc:
        print(f"pivotctl: cannot reach Pivot bridge: {exc}", file=sys.stderr)
        return 1

    try:
        s.sendall((args.cmd + "\n").encode("utf-8"))
        data = s.recv(4096)
        sys.stdout.write(data.decode("utf-8", "replace"))
        if not data.endswith(b"\n"):
            sys.stdout.write("\n")
    finally:
        s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
