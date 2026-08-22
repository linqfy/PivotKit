#!/usr/bin/env python3
"""pkpython.py - Python-side SDK for PivotKit.

Drives a running Pivot Animator + PivotKit instance through the mod bridge
(TCP 127.0.0.1:50077, the proven v1 console protocol). Use it:

  * from spawned GUI scripts (build tkinter/PySide interfaces that control
    Pivot),
  * interactively (python -i tools/pkpython.py),
  * for heavy computation that would stall the Lua tick (do it here, poke
    results back).

The Lua side must have pivotlib2 loaded (mods/00_pivotlib2.lua) - commands
are evaluated as Lua with `pl2` in scope.

Example:
    from pkpython import Pivot
    p = Pivot()
    print(p.frame_count())
    p.eval("pl2.frame(0):set_tween(5)")
    for fig in p.figures(0):
        print(fig["color"])
"""
import json
import socket

HOST, PORT = "127.0.0.1", 50077


class Pivot:
    def __init__(self, host=HOST, port=PORT, timeout=10.0):
        self.host, self.port, self.timeout = host, port, timeout

    def close(self):
        pass  # connection-per-command (matches the v1 bridge protocol)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ---- core ------------------------------------------------------------
    def eval(self, lua_expr):
        """Evaluate a Lua expression (pivotlib2 in scope); returns its result
        as a string, or raises RuntimeError on Lua errors.

        The v1 bridge serves one command per connection and closes the
        socket after replying, so we connect, send, and read to EOF."""
        with socket.create_connection((self.host, self.port),
                                      self.timeout) as s:
            s.settimeout(self.timeout)
            s.sendall((lua_expr.strip() + "\n").encode())
            try:
                s.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            out = b""
            while True:
                chunk = s.recv(4096)
                if not chunk:
                    break
                out += chunk
        text = out.decode("utf-8", "replace").strip()
        if text.startswith("error:"):
            raise RuntimeError(text)
        return text

    def try_eval(self, lua_expr, default=None):
        try:
            return self.eval(lua_expr)
        except RuntimeError:
            return default

    # ---- typed conveniences (mirror pivotlib2) -----------------------------
    def frame_count(self):
        return int(self.try_eval("pl2.frame_count()", "0") or 0)

    def figure_count(self, frame):
        return int(self.try_eval(f"pl2.frame({frame}):figure_count()", "0") or 0)

    def frame(self, i):
        return FrameRef(self, i)

    def figures(self, frame):
        for i in range(self.figure_count(frame)):
            yield FigureRef(self, frame, i)

    def select_frame(self, i):
        self.eval(f"pl2.select_frame({i})")

    def set_num_frames(self, n):
        self.eval(f"pl2.set_num_frames({n})")

    def redraw(self):
        self.eval("pl2.redraw()")

    def main_form(self):
        return self.eval("pivot.address(pivot.get_main_form())")


class FrameRef:
    def __init__(self, pivot, index):
        self.p, self.i = pivot, index

    def __getitem__(self, key):
        return self.p.try_eval(f"pl2.frame({self.i}):get_{key}()", "nil")

    def __setitem__(self, key, value):
        if isinstance(value, str):
            value = f'"{value}"'
        self.p.eval(f"pl2.frame({self.i}):set_{key}({value})")

    def tween(self):
        return int(self["tween"] or 0)

    def set_tween(self, v):
        self["tween"] = v

    def camera(self):
        return json.loads(self.p.try_eval(
            f"local c=pl2.frame({self.i}):get_camera() "
            "return string.format('[%%f,%%f,%%f,%%f]',c.x,c.y,c.angle,c.scale)",
            "[0,0,0,1]"))

    def set_camera(self, x=None, y=None, angle=None, scale=None):
        parts = []
        if x is not None: parts.append(f"x={x}")
        if y is not None: parts.append(f"y={y}")
        if angle is not None: parts.append(f"angle={angle}")
        if scale is not None: parts.append(f"scale={scale}")
        self.p.eval(f"pl2.frame({self.i}):set_camera({{ {', '.join(parts)} }})")


class FigureRef:
    def __init__(self, pivot, frame, index):
        self.p, self.frame_i, self.i = pivot, frame, index

    def _lua(self):
        return f"pl2.frame({self.frame_i}):figure({self.i})"

    def __getitem__(self, key):
        return self.p.try_eval(f"{self._lua()}:get_{key}()", "nil")

    def __setitem__(self, key, value):
        self.p.eval(f"{self._lua()}:set_{key}({value})")

    def color(self):
        return int(self["color"] or 0)

    def set_color(self, argb):
        self["color"] = argb

    def move(self, dx, dy):
        self.p.eval(f"{self._lua()}:move({dx}, {dy})")

    def vertex(self, i):
        x = float(self.p.try_eval(f"{self._lua()}:vertex({i}).x", "0") or 0)
        y = float(self.p.try_eval(f"{self._lua()}:vertex({i}).y", "0") or 0)
        return x, y

    def set_vertex(self, i, x, y):
        self.p.eval(f"{self._lua()}:set_vertex({i}, {x}, {y})")


if __name__ == "__main__":
    p = Pivot()
    print("main form @", p.main_form())
    print("frames:", p.frame_count())
    f0 = p.frame(0)
    print("frame0 figures:", p.figure_count(0), "tween:", f0.tween())
    print("camera:", p.camera())
