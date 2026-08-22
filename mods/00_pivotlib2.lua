-- pivotlib2.lua - typed Pivot 5.2.11 API over the pivotkit v1 primitives.
--
-- Everything here is driven by the RE database (research/ in this repo):
--   TMainForm+0x654 = TFrameSequence (TArray<TFigures>) - the animation
--   TFigures        = one frame   (FiguresUnit)
--   TFigure         = one figure  (FigureUnit)
-- Offsets are for Pivot 5.2.11 (sha256 2c7911d3...); see research/classes/.
--
-- Requires the legacy runtime's `pivot` table (peek/read_u32/write_u32/
-- get_main_form/call/...). Pure Lua - no C rebuild needed.

local PL2 = { _VERSION = "pivotlib2 0.1 (Pivot 5.2.11)" }

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

function PL2.select_frame(i)        return PL2.app_call("SelectFrame", i) end
function PL2.set_num_frames(n)      return PL2.app_call("SetNumFrames", n) end
function PL2.stop_playback()        return PL2.app_call("StopButtonClick") end

function PL2.redraw()               -- force a frame redraw
    return PL2.app_call("DrawFrameNumber")
end

-- ---------- Python bridge (see mods/04_python_bridge.lua) ------------------

PL2.python = { run = function() return nil, "python bridge not loaded" end }

-- ---------- export ---------------------------------------------------------

_G.pivotlib2 = PL2
_G.pl2 = PL2
return PL2
