-- 04_python_bridge.lua - run Python from PivotKit mods.
--
-- Two modes:
--   pl2.python.run(code)      -> stdout string (synchronous; blocks the tick,
--                                keep scripts short or use spawn)
--   pl2.python.spawn(script)  -> detached pythonw process (for GUIs built
--                                with tkinter/PySide/etc.; the script can
--                                drive Pivot back through the TCP bridge
--                                using tools/pkpython.py)
--
-- Requires python.exe / pythonw.exe on PATH (or set PIVOTKIT_PYTHON).

local PYTHON = os.getenv("PIVOTKIT_PYTHON") or "python"
local PYTHONW = os.getenv("PIVOTKIT_PYTHONW") or "pythonw"

local seq = 0

local function tmp_dir()
    local t = os.getenv("TEMP") or ""
    if t:match("^%a:\\") then return t end          -- proper Windows path
    local u = os.getenv("USERPROFILE") or "."
    return u .. "\\AppData\\Local\\Temp"
end

local function tmp_script(code)
    seq = seq + 1
    local path = string.format("%s\\pkpy_%d_%d.py", tmp_dir(), os.time(), seq)
    local f = io.open(path, "wb")
    if not f then return nil end
    f:write(code)
    f:close()
    return path
end

local function run(code)
    local path = tmp_script(code)
    if not path then return nil, "cannot write temp script" end
    -- NB: quote only args; this Lua's popen breaks with a quoted exe.
    local p = io.popen(string.format('%s -X utf8 "%s" 2>&1', PYTHON, path))
    if not p then os.remove(path) return nil, "popen failed" end
    local out = p:read("a")
    p:close()
    os.remove(path)
    return out
end

local function spawn(script_path)
    -- Detached GUI process; it outlives this tick.
    os.execute(string.format('cmd /c start "" /B %s -X utf8 "%s"',
                             PYTHONW, script_path))
    return true
end

local function spawn_code(code)
    local path = tmp_script(code)
    if not path then return nil, "cannot write temp script" end
    return spawn(path)
end

-- One-shot examples:
--   local out = pl2.python.run("print(sum(range(10)))")
--   pl2.python.spawn_code([[
--       import sys; sys.path.insert(0, r"<pivotkit>/tools")
--       from pkpython import Pivot
--       p = Pivot()
--       p.eval("pl2.select_frame(0)")
--       import tkinter; tkinter.Tk().mainloop()
--   ]])

if pivotlib2 then
    pivotlib2.python = { run = run, spawn = spawn, spawn_code = spawn_code }
end

_G.pkpython = pivotlib2 and pivotlib2.python or { run = run, spawn = spawn,
                                                  spawn_code = spawn_code }
