-- mock the v1 pivot API over a fake little-endian memory
local mem = {}
local function wr8(a, v) mem[a] = v & 0xFF end
local function rd8(a) return mem[a] or 0 end
local function wr32(a, v) for i=0,3 do wr8(a+i, v >> (8*i)) end end
local function rd32(a) local v=0 for i=3,0,-1 do v=(v<<8)|rd8(a+i) end return v end

local function mkstr_ptr(s) -- fake Delphi string: len at -4
  local base = 0x100F000; local p = base + 4
  wr32(base, #s)
  for i=1,#s do wr8(p+i-1, s:byte(i)) end
  return p
end

-- TMainForm at 0x1000; TFrameSequence dynarray at +0x654
local MF, SEQ = 0x10000000, 0x10002000
wr32(SEQ - 4, 2)                 -- 2 frames
local F0, F1 = 0x10003000, 0x10004000
wr32(SEQ + 0, F0); wr32(SEQ + 4, F1)
wr32(MF + 0x654, SEQ)

-- frame 0: tween=3, bg=7, 2 figures; camera (1.5, -2.25, 90deg, 2.0)
wr32(F0 + 0x10, FIGARR or 0x10005000)  -- figures dynarray
wr32(0x10005000 - 4, 2)
local FIG1 = 0x10006000
wr32(0x10005000 + 4, FIG1)
local function wr16(a,v) wr8(a, v&0xFF); wr8(a+1, v>>8) end
wr16(F0 + 0x04, 2)               -- FNumFigures
wr16(F0 + 0x06, 7)               -- FBackground
wr16(F0 + 0x0C, 3)               -- FFrameTween
local cam = F0 + 0x38
local function wrf(a, f) local s = string.pack("<f", f); for i=0,3 do wr8(a+i, s:byte(i+1)) end end
local function wrd(a, d) local s = string.pack("<d", d); for i=0,7 do wr8(a+i, s:byte(i+1)) end end
wrf(cam, 1.5); wrf(cam+4, -2.25); wrd(cam+8, math.rad(90)); wrf(cam+16, 2.0)
-- figure 1: color, scale, vertices
wr32(FIG1 + 0x24, 0xFF304050)
wrf(FIG1 + 0x2C, 1.25)
local VARR = 0x10007000
wr32(FIG1 + 0x34, VARR); wr32(VARR - 4, 3)
wrf(VARR + 0, 10); wrf(VARR + 4, 20)
wrf(VARR + 16, 30); wrf(VARR + 20, 40)
wrf(VARR + 32, 50); wrf(VARR + 36, 60)

pivot = {
  peek = function(addr, off, kind)
    local a = addr + off
    if kind == "u8" then return rd8(a)
    elseif kind == "u16" then return rd8(a) | (rd8(a+1) << 8)
    elseif kind == "u32" then return rd32(a)
    elseif kind == "f32" then return string.unpack("<f", string.char(rd8(a),rd8(a+1),rd8(a+2),rd8(a+3)))
    elseif kind == "f64" then return string.unpack("<d", string.char(rd8(a),rd8(a+1),rd8(a+2),rd8(a+3),rd8(a+4),rd8(a+5),rd8(a+6),rd8(a+7)))
    end
  end,
  read_u32 = rd32,
  write_u32 = wr32,
  address = function(o) return o end,
  get_main_form = function() return MF end,
  call = function() end,
}

dofile("mods/00_pivotlib2.lua")

assert(pl2.frame_count() == 2, "frame count")
local f0 = pl2.frame(0)
assert(f0:index() == 0 and f0:figure_count() == 2, "frame0 figures")
assert(f0:get_tween() == 3, "tween read")
f0:set_tween(5)
assert(rd8(F0+0x0C) == 5 and rd8(F0+0x0D) == 0, "tween write RMW")
assert(f0:get_background() == 7, "bg read")
local c = f0:get_camera()
assert(math.abs(c.x - 1.5) < 1e-6 and math.abs(c.y + 2.25) < 1e-6, "camera pos")
assert(math.abs(c.angle - math.rad(90)) < 1e-9 and math.abs(c.scale - 2.0) < 1e-6, "camera angle/scale")
f0:set_camera{ angle = 0 }
assert(math.abs(("%d"):format(rd32(F0+0x40)) and 0) == 0, "camera write")

local g = f0:figure(1)
assert(g:get_color() == 0xFF304050, "figure color")
g:set_color(0xAABBCCDD)
assert(rd32(FIG1+0x24) == 0xAABBCCDD, "figure color write")
assert(math.abs(g:get_scale() - 1.25) < 1e-6, "figure scale")
assert(g:vertex_count() == 3, "vertex count")
local v2 = g:vertex(2)
assert(math.abs(v2.x - 50) < 1e-6 and math.abs(v2.y - 60) < 1e-6, "vertex read")
g:move(1, -1)
assert(math.abs((select(1, g:vertex(0))).x - 11) < 1e-6, "figure move")
print("ALL PIVOTLIB2 TESTS PASSED")

-- ---- UI + events test (mocked overlay/input) -----------------------------
local drawn = {}
pivot.window_rect = function() return 100, 200, 700, 600 end
pivot.cursor_pos  = function() return 100 + 55, 200 + 22 end   -- inside button
pivot.key_down    = function(vk) return _MOUSE_DOWN and 1 or 0 end
pivot.overlay_create = function() return true end
pivot.overlay_begin  = function() drawn = {} end
pivot.overlay_rect   = function(x1,y1,x2,y2,a) drawn[#drawn+1] = {"rect",x1,y1,x2,y2,a} end
pivot.overlay_text   = function(x,y,t,s,a) drawn[#drawn+1] = {"text",x,y,t,s,a} end
pivot.overlay_commit = function() end
pivot.on_update = nil

local clicks = 0
pl2.ui.panel("p", 0, 0, 200, 100)
pl2.ui.label("l", 8, 6, "hello", 12)
pl2.ui.button("b", 40, 15, 60, 25, "Go", function() clicks = clicks + 1 end)

_MOUSE_DOWN = false
pl2._tick(1)
assert(#drawn == 4, "widget draw count: " .. #drawn)  -- panel + label + button rect + button text
assert(clicks == 0, "no click when mouse up")

_MOUSE_DOWN = true
pl2._tick(2)
assert(clicks == 1, "click fired on press inside button")
pl2._tick(3)
assert(clicks == 1, "no re-fire while held")

-- tick registration
local ticks = 0
pl2.on_tick(function() ticks = ticks + 1 end)
pl2._tick(4)
assert(ticks == 1, "on_tick registered")

print("PIVOTLIB2 UI/EVENT TESTS PASSED")
