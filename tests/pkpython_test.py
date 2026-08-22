#!/usr/bin/env python3
"""Test pkpython.Pivot against a simulated v1 bridge (one command per
connection, server closes after replying). Run: python tests/pkpython_test.py"""
import threading, socket, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))

def handle(c):
    expr = c.makefile('rb').readline().decode().strip()
    resp = {"pl2.frame_count()": b"7",
            "pl2.frame(0):figure_count()": b"3"}.get(expr, b"5" if "tween" in expr
                                                     else (b"1908871432" if "address" in expr else b"ok"))
    c.sendall(resp); c.close()

sock = socket.socket(); sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('127.0.0.1', 50123)); sock.listen(4)
threading.Thread(target=lambda: [threading.Thread(target=handle, args=(c,), daemon=True).start()
                                 for c in iter(sock.accept, None)] if False else None, daemon=True)
def server():
    while True:
        try: c, _ = sock.accept()
        except OSError: return
        threading.Thread(target=handle, args=(c,), daemon=True).start()
threading.Thread(target=server, daemon=True).start()

from pkpython import Pivot
p = Pivot(port=50123)
assert p.frame_count() == 7 and p.frame(0).tween() == 5 and p.figure_count(0) == 3
assert p.eval("1+1") == "ok" and p.main_form() == "1908871432"
print("PKPYTHON CLIENT TEST PASSED")
sock.close()
