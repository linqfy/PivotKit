-- pivotlib2: typed API over the runtime pivot primitives.
-- Offsets are RE-derived for 5.2.11 (docs/research/classes/):
-- TMainForm+0x654 = TFrameSequence (TArray<TFigures>); TFigures = frame;
-- TFigure = figure. Pure Lua; no C rebuild needed.

local PL2 = { _VERSION = "pivotlib 0.3.0 (Pivot 5.2.11)" }

-- ---------- low-level helpers ---------------------------------------------

local peek = pivot.peek
local r32  = pivot.read_u32

local function addr_of(obj)           -- lightuserdata -> integer address
    if obj == nil then return 0 end
    return pivot.address(obj)
end

local function ok_ptr(v)
    return v and v > 0x10000 and v < 0xFFFFFFFF and r32(v) ~= nil
end

-- Read-modify-write pokes (v1 only exposes write_u32).
local function poke_bytes(addr, b1, b2, b3, b4)
    local base = addr - (addr % 4)
    local old = r32(base)
    if not old then return false end
    local new = old
    local function put(byte, at)
        local sh = 8 * (at - base)
        new = (new & ~(0xFF << sh)) | ((byte & 0xFF) << sh)
    end
    put(b1, addr)
    if b2 then put(b2, addr + 1) end
    if b3 then put(b3, addr + 2) end
    if b4 then put(b4, addr + 3) end
    return pivot.write_u32(base, new)
end

local function poke16(addr, v)      return poke_bytes(addr, v & 0xFF, v >> 8) end
local function poke8(addr, v)       return poke_bytes(addr, v & 0xFF) end
local function pokef32(addr, f)
    local s = string.pack("<f", f)
    return poke_bytes(addr, s:byte(1), s:byte(2), s:byte(3), s:byte(4))
end
local function pokedb64(addr, d)
    local s = string.pack("<d", d)
    for i = 0, 3 do
        local a = addr + i * 2
        poke_bytes(a, s:byte(i * 2 + 1), s:byte(i * 2 + 2))
    end
end

-- ---------- the animation (TMainForm.TFrameSequence @ +0x654) ------------

local MF_FRAMESEQ = 0x654

local Frame = {}
Frame.__index = Frame

local Figure = {}
Figure.__index = Figure

local function dyn_count(ptr)        -- Delphi dynarray: length at ptr-4
    if not ok_ptr(ptr) then return 0 end
    local n = r32(ptr - 4)
    if not n or n > 1000000 then return 0 end
    return n
end

function PL2.main_form()
    return pivot.get_main_form()
end

function PL2.frame_count()
    local mf = addr_of(PL2.main_form())
    if mf == 0 then return 0 end
    local seq = peek(mf, MF_FRAMESEQ, "u32")
    return dyn_count(seq)
end

function PL2.frame(i)                -- 0-based
    local mf = addr_of(PL2.main_form())
    if mf == 0 then return nil end
    local seq = peek(mf, MF_FRAMESEQ, "u32")
    if i < 0 or i >= dyn_count(seq) then return nil end
    local self = r32(seq + 4 * i)
    if not ok_ptr(self) then return nil end
    return setmetatable({ _a = self, _i = i }, Frame)
end

function PL2.frames()                -- iterator
    local n = PL2.frame_count()
    local i = -1
    return function()
        i = i + 1
        if i < n then return PL2.frame(i) end
    end
end

-- ---------- Frame (TFigures, size 0x54) ------------------------------------

function Frame:index()      return self._i end
function Frame:addr()       return self._a end

function Frame:figure_count()
    return peek(self._a, 0x04, "u16") or 0
end

function Frame:figure(i)    -- 0-based
    local arr = peek(self._a, 0x10, "u32")
    if i < 0 or i >= dyn_count(arr) then return nil end
    local fa = r32(arr + 4 * i)
    if not ok_ptr(fa) then return nil end
    return setmetatable({ _a = fa, _i = i, _frame = self }, Figure)
end

function Frame:figures()
    local n = self:figure_count()
    local i = -1
    return function()
        i = i + 1
        if i < n then return self:figure(i) end
    end
end

Frame.get_tween       = function(f) return peek(f._a, 0x0C, "u16") or 0 end
Frame.set_tween       = function(f, v) return poke16(f._a + 0x0C, v) end
Frame.get_background  = function(f) return peek(f._a, 0x06, "u16") or 0 end
Frame.set_background  = function(f, v) return poke16(f._a + 0x06, v) end
Frame.get_bg2         = function(f) return peek(f._a, 0x08, "u16") or 0 end
Frame.set_bg2         = function(f, v) return poke16(f._a + 0x08, v) end
Frame.get_bg_ratio    = function(f) return peek(f._a, 0x0A, "u8") or 0 end
Frame.set_bg_ratio    = function(f, v) return poke8(f._a + 0x0A, v) end

-- TCamera inline at +0x38: Pos(TPointF) @0, Angle(double) @8, Scale(single) @16
Frame.get_camera = function(f)
    return {
        x     = peek(f._a, 0x38, "f32") or 0,
        y     = peek(f._a, 0x3C, "f32") or 0,
        angle = peek(f._a, 0x40, "f64") or 0,
        scale = peek(f._a, 0x48, "f32") or 1,
    }
end
Frame.set_camera = function(f, c)
    local a = f._a
    if c.x     then pokef32(a + 0x38, c.x) end
    if c.y     then pokef32(a + 0x3C, c.y) end
    if c.angle then pokedb64(a + 0x40, c.angle) end
    if c.scale then pokef32(a + 0x48, c.scale) end
    return true
end

-- ---------- Figure (TFigure, size 0x64) ------------------------------------

function Figure:index()      return self._i end
function Figure:addr()       return self._a end
function Figure:frame()      return self._frame end

Figure.get_color        = function(g) return peek(g._a, 0x24, "u32") or 0 end
Figure.set_color        = function(g, c) return pivot.write_u32(g._a + 0x24, c) end
Figure.get_transparency = function(g) return peek(g._a, 0x28, "u8") or 0 end
Figure.set_transparency = function(g, v) return poke8(g._a + 0x28, v) end
Figure.get_scale        = function(g) return peek(g._a, 0x2C, "f32") or 1 end
Figure.set_scale        = function(g, v) return pokef32(g._a + 0x2C, v) end
Figure.get_type_index   = function(g) return peek(g._a, 0x30, "u16") or 0 end
Figure.get_flipped      = function(g) return (peek(g._a, 0x3C, "u8") or 0) ~= 0 end
Figure.set_flipped      = function(g, v) return poke8(g._a + 0x3C, v and 1 or 0) end
Figure.get_draw_order   = function(g) return peek(g._a, 0x4C, "u16") or 0 end
Figure.get_id           = function(g) return peek(g._a, 0x58, "i32") or 0 end

function Figure:vertex_count()
    return dyn_count(peek(self._a, 0x34, "u32"))
end

function Figure:vertex(i)     -- TFigVertex: x f32 @0, y f32 @4 (16-byte stride)
    local arr = peek(self._a, 0x34, "u32")
    if i < 0 or i >= dyn_count(arr) then return nil end
    local v = arr + 16 * i
    return { x = peek(v, 0, "f32") or 0, y = peek(v, 4, "f32") or 0 }
end

function Figure:set_vertex(i, x, y)
    local arr = peek(self._a, 0x34, "u32")
    if i < 0 or i >= dyn_count(arr) then return false end
    local v = arr + 16 * i
    if x then pokef32(v, x) end
    if y then pokef32(v + 4, y) end
    return true
end

function Figure:move(dx, dy)
    for i = 0, self:vertex_count() - 1 do
        local v = self:vertex(i)
        self:set_vertex(i, v.x + dx, v.y + dy)
    end
    return self
end

-- ---------- app actions (published methods - safe to call by name) --------

function PL2.app_call(method, ...)
    local mf = PL2.main_form()
    if not mf then return nil end
    return pivot.call(mf, method, ...)
end

function PL2.select_frame(i) return PL2.app_call("SelectFrame", i) end
function PL2.set_num_frames(n) return PL2.app_call("SetNumFrames", n) end
function PL2.stop_playback() return PL2.app_call("StopButtonClick") end

function PL2.redraw()               -- force a frame redraw
    return PL2.app_call("DrawFrameNumber")
end

-- ---------- Python bridge (see mods/04_python_bridge.lua) ------------------

PL2.python = { run = function() return nil, "python bridge not loaded" end }

-- ---------- export ---------------------------------------------------------

_G.pivotlib2 = PL2
_G.pl2 = PL2

-- ---------- drawn UI widgets (over the v1 overlay canvas) -----------------
-- A lightweight retained UI: panels, labels, buttons with hit-testing on
-- the main window's client rect. Redrawn every tick via the overlay.

local Nav                       -- forward: nav button (defined below)
local nav_draw                  -- forward: nav rendering (defined below)

local UI = { widgets = {}, next_id = 1, dirty = true }
PL2.ui = UI

function UI.clear()
    UI.widgets = {}
    UI.dirty = true
end

function UI.panel(id, x, y, w, h, argb)
    UI.widgets[#UI.widgets + 1] =
        { kind = "panel", id = id, x = x, y = y, w = w, h = h, argb = argb or 0xC0202020 }
    UI.dirty = true
end

function UI.label(id, x, y, text, size, argb)
    UI.widgets[#UI.widgets + 1] =
        { kind = "label", id = id, x = x, y = y, text = tostring(text),
          size = size or 12, argb = argb or 0xFFFFFFFF }
    UI.dirty = true
end

function UI.button(id, x, y, w, h, text, fn, argb)
    UI.widgets[#UI.widgets + 1] =
        { kind = "button", id = id, x = x, y = y, w = w, h = h,
          text = tostring(text), fn = fn, argb = argb or 0xFF3050A0,
          state = "up" }
    UI.dirty = true
end

local function hit(w, mx, my)
    return mx >= w.x and mx < w.x + w.w and my >= w.y and my < w.y + w.h
end

local mouse_was_down = false

function UI.tick()                       -- call from on_update; also drives the nav button
    local wx, wy, wr, wb = pivot.window_rect()
    local cx, cy = pivot.cursor_pos()
    local mx, my = cx - wx, cy - wy      -- client coords
    local down = (pivot.key_down(0x01) == true) or (pivot.key_down(0x01) == 1)

    local clicked = down and not mouse_was_down
    Nav.tick(mx, my, down, clicked)
    for _, w in ipairs(UI.widgets) do
        if w.kind == "button" then
            local h = hit(w, mx, my)
            if down and h and not mouse_was_down and w.fn then
                local ok, err = pcall(w.fn)
                if not ok then pivot.log("ui button error: " .. tostring(err)) end
            end
        end
    end
    mouse_was_down = down

    -- draw (cheap: every tick keeps text/positions fresh)
    if not pivot.overlay_create then return end   -- v1 overlay not available
    pivot.overlay_begin()
    for _, w in ipairs(UI.widgets) do
        if w.kind == "panel" then
            pivot.overlay_rect(w.x, w.y, w.x + w.w, w.y + w.h, w.argb)
        elseif w.kind == "label" then
            pivot.overlay_text(w.x, w.y, w.text, w.size, w.argb)
        elseif w.kind == "button" then
            pivot.overlay_rect(w.x, w.y, w.x + w.w, w.y + w.h, w.argb)
            pivot.overlay_text(w.x + 4, w.y + w.h / 2 - 7, w.text, 12, 0xFFFFFFFF)
        end
    end
    nav_draw(wr - wx, wb - wy)
    pivot.overlay_commit()
    UI.dirty = false
end

-- ---------- event presets (v1 published-method hooks) ----------------------

local Events = {}
PL2.events = Events

local function hook_method(name, fn)
    local mf = PL2.main_form()
    if not mf then return false end
    return pivot.hook(mf, name, fn) == true or pivot.hook(mf, name, fn) == 1
end

-- usage: pl2.events.on_frame_change(function(idx) ... end)
function Events.on_frame_change(fn)
    return hook_method("SelectFrame", function(self, idx)
        local ok, err = pcall(fn, idx)
        if not ok then pivot.log("frame_change error: " .. tostring(err)) end
    end)
end

function Events.on_stop(fn)
    return hook_method("StopButtonClick", function()
        local ok, err = pcall(fn)
        if not ok then pivot.log("stop error: " .. tostring(err)) end
    end)
end

function Events.on_set_frames(fn)
    return hook_method("SetNumFrames", function(self, n)
        local ok, err = pcall(fn, n)
        if not ok then pivot.log("set_frames error: " .. tostring(err)) end
    end)
end

-- pl2.tick chain: mods register here; UI ticks automatically.
local tick_fns = {}
function PL2.on_tick(fn)
    tick_fns[#tick_fns + 1] = fn
end

function PL2._tick(frame)
    UI.tick()
    for _, fn in ipairs(tick_fns) do
        local ok, err = pcall(fn, frame)
        if not ok then pivot.log("tick error: " .. tostring(err)) end
    end
end

-- auto-install the global tick if the host exposes on_update
if pivot.on_update then
    pivot.on_update(function(frame)
        local ok, err = pcall(PL2._tick, frame)
        if not ok then pivot.log("pl2 tick: " .. tostring(err)) end
    end)
end


-- ---------- raw-address calls/hooks (pivotkit.dll with call_addr support) --
-- These reach ANY function in the binary, including the ~3500 internal
-- functions mapped in Ghidra (see docs/research/ + include/pivot/ bindings).
-- Only use addresses verified for the running build.

function PL2.call_addr(fn_addr, self, a1, a2, ...)
    return pivot.call_addr(self or 0, fn_addr, a1 or 0, a2 or 0, ...)
end

function PL2.hook_addr(fn_addr, callback)
    return pivot.hook_addr(fn_addr, callback)
end

function PL2.unhook_addr(fn_addr)
    return pivot.unhook_addr(fn_addr)
end

-- ---------- PivotKit nav button (top-right) + menu ------------------------

Nav = { open = false, page = nil, page_data = nil }
PL2.nav = Nav

local LOGO = {
    "  ____ _       _    _  ____ _   _ ___ ",
    " |  _(_)_ __ | | _/ ||  _ (_) |_|_ _| ",
    " | |_) | '_ \\| |/ / || |_) |  _ || |  ",
    " |  __| |_) |   <  ||  __/| | || || |  ",
    " |_|  | .__/|_|\\\\ ||_|   |_| |_||___|",
    "     |_|            |__|              ",
}

-- Mod load order == sort order of the compiled mod files.
function Nav.mod_list()
    local names = {}
    local p = io.popen('dir /b /on "mods/compiled/*.lc" 2>nul')
    if p then
        for line in p:lines() do
            line = line:gsub("%.lc$", "")
            names[#names + 1] = line
        end
        p:close()
    end
    if #names == 0 then
        p = io.popen('dir /b /on "pivotkit/mods/compiled/*.lc" 2>nul')
        if p then
            for line in p:lines() do
                line = line:gsub("%.lc$", "")
                names[#names + 1] = line
            end
            p:close()
        end
    end
    return names
end

function Nav.about_info()
    local mf = PL2.main_form()
    local base = 0
    if pivot.class then
        local ct = pivot.class("TMainForm")
        if ct and ct ~= 0 then base = ct - 0x714D68 end  -- ct - (VMT - image base)
    end
    return {
        lib      = PL2._VERSION or "pivotlib",
        target   = "Pivot Animator 5.2.11 (2c7911d3)",
        app_base = string.format("0x%X", base ~= 0 and base or 0),
        frames   = tostring(PL2.frame_count()),
        mainform = mf and string.format("0x%X", pivot.address(mf)) or "n/a",
    }
end

function nav_draw(w, h)
    local bx, by, bw, bh = w - 96, 4, 88, 20
    pivot.overlay_rect(bx, by, bx + bw, by + bh, 0xE6305080)
    pivot.overlay_text(bx + 10, by + 4, "PivotKit", 12, 0xFFFFFFFF)
    Nav._box = { bx, by, bw, bh }
    if Nav.open then
        local mx = bx
        local my = by + bh + 2
        pivot.overlay_rect(mx, my, mx + bw, my + 42, 0xF0202020)
        pivot.overlay_rect(mx + 2, my + 2, mx + bw - 2, my + 20, 0xFF3A3A50)
        pivot.overlay_text(mx + 8, my + 4, "About", 12, 0xFFFFFFFF)
        pivot.overlay_rect(mx + 2, my + 22, mx + bw - 2, my + 40, 0xFF3A3A50)
        pivot.overlay_text(mx + 8, my + 24, "Mod List", 12, 0xFFFFFFFF)
        Nav._items = {
            { mx + 2, my + 2,  mx + bw - 2, my + 20, "about" },
            { mx + 2, my + 22, mx + bw - 2, my + 40, "mods" },
        }
    else
        Nav._items = nil
    end
    if Nav.page == "about" then
        local px, py = math.floor(w / 2) - 190, 60
        pivot.overlay_rect(px, py, px + 380, py + 190, 0xF6161622)
        for i, line in ipairs(LOGO) do
            pivot.overlay_text(px + 14, py + 8 + (i - 1) * 13, line, 12, 0xFF7FD0FF)
        end
        local info = Nav.about_info()
        local rows = {
            "library   : " .. tostring(info.lib),
            "target    : " .. info.target,
            "app base  : " .. info.app_base,
            "main form : " .. info.mainform,
            "frames    : " .. info.frames,
        }
        for i, r in ipairs(rows) do
            pivot.overlay_text(px + 14, py + 96 + (i - 1) * 15, r, 12, 0xFFE0E0E0)
        end
        pivot.overlay_text(px + 14, py + 172, "[M] more info (opens repo)   [Esc] close", 12, 0xFF9A9AB0)
        Nav._page_box = { px, py, 380, 190 }
    elseif Nav.page == "mods" then
        local list = Nav.mod_list()
        local ph = 46 + #list * 15
        local px, py = math.floor(w / 2) - 190, 60
        pivot.overlay_rect(px, py, px + 380, py + ph, 0xF6161622)
        pivot.overlay_text(px + 14, py + 8, "Loaded mods (load order)", 12, 0xFF7FD0FF)
        for i, n in ipairs(list) do
            pivot.overlay_text(px + 14, py + 28 + (i - 1) * 15,
                              string.format("%2d. %s", i, n), 12, 0xFFE0E0E0)
        end
        pivot.overlay_text(px + 14, py + ph - 18, "[Esc] close", 12, 0xFF9A9AB0)
        Nav._page_box = { px, py, 380, ph }
    else
        Nav._page_box = nil
    end
end

function Nav.tick(mx, my, down, clicked)
    if not pivot.overlay_rect then return end
    local wx, wy, wr, wb = pivot.window_rect()
    local w, h = wr - wx, wb - wy
    local b = Nav._box
    if clicked and b and mx >= b[1] and mx < b[1] + b[3] and my >= b[2] and my < b[2] + b[4] then
        Nav.open = not Nav.open
        return
    end
    if Nav.open and clicked then
        local consumed = false
        if Nav._items then
            for _, it in ipairs(Nav._items) do
                if mx >= it[1] and mx < it[3] and my >= it[2] and my < it[4] then
                    Nav.open = false
                    Nav.page = (it[5] == "about") and "about" or "mods"
                    consumed = true
                    break
                end
            end
        end
        local in_box = b and mx >= b[1] and mx < b[1] + b[3] and my >= b[2] and my < b[2] + b[4]
        if not consumed and not in_box then Nav.open = false end
    end
    if Nav.page then
        if pivot.key_down and (pivot.key_down(0x1B) == 1 or pivot.key_down(0x1B) == true) then
            Nav.page = nil
        end
        if Nav.page == "about" and pivot.key_down and (pivot.key_down(0x4D) == 1 or pivot.key_down(0x4D) == true) then
            os.execute('cmd /c start "" "https://github.com/linqfy/PivotKit"')
        end
    end
end

-- ---------- library merge ---------------------------------------------------
-- One library, no version split: if the legacy pivotlib (00_pivotlib.lua)
-- loaded first, fold this API into it (legacy names win on collision, so
-- nothing is lost). pl2/pivotlib2 remain as aliases.

if _G.pivotlib and type(_G.pivotlib) == "table" then
    for k, v in pairs(PL2) do
        if rawget(_G.pivotlib, k) == nil then
            rawset(_G.pivotlib, k, v)
        end
    end
    _G.pivotlib.v2 = PL2
end

return PL2
