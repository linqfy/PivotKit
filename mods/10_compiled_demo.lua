-- 10_compiled_demo.lua - compiled-mod demo.
--
-- This file ships as Lua BYTECODE (mods/compiled/10_compiled_demo.lc via
-- tools/pkcompile.py) - the runtime never interprets the source. The Python
-- block below is extracted and py_compile'd to pymods/__pycache__/ at the
-- same time; pl2.python.block() executes that compiled bytecode on the full
-- system CPython (any installed library is importable).

--[[ @python stats_demo
import json, math, sys, platform

# Any pip-installed library works here too (numpy, requests, PySide...):
#     import numpy as np
data = {
    "python": platform.python_version(),
    "pi": round(math.pi, 10),
    "argv_mode": "compiled-block",
}
print(json.dumps(data))
-- @end ]]

if pivotlib2 and pivotlib2.python and pivotlib2.python.block then
    -- run the compiled python block (bytecode), capture its stdout
    local out = pivotlib2.python.block("10_compiled_demo__stats_demo")
    pivot.log("python block said: " .. tostring(out))

    -- typed API still available:
    --   pl2.frame_count(), pl2.frame(0):set_tween(5), pl2.ui.button(...)
end
