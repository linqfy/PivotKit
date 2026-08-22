-- merged_lib_test.lua - verify legacy pivotlib + pivotlib2 merge into ONE
-- library without losing either side's API. Run: bin/lua.exe tests/merged_lib_test.lua

local mem = {}
local function wr8(a, v) mem[a] = v & 0xFF end
local function rd8(a) return mem[a] or 0 end
local function wr32(a, v) for i = 0, 3 do wr8(a + i, v >> (8 * i)) end end
local function rd32(a) local v = 0 for i = 3, 0, -1 do v = (v << 8) | rd8(a + i) end return v end

local MF, SEQ, F0 = 0x10000000, 0x10002000, 0x10003000
wr32(SEQ - 4, 1); wr32(SEQ, F0); wr32(MF + 0x654, SEQ)
wr32(F0 + 0x10, 0x10005000)         -- figures array
wr32(0x10005000 - 4, 0)

local mock_form = {}
pivot = {
    peek = function(addr, off, kind)
        local a = addr + off
        if kind == "u8" then return rd8(a)
        elseif kind == "u16" then return rd8(a) | (rd8(a + 1) << 8)
        elseif kind == "u32" then return rd32(a)
        elseif kind == "f32" then return string.unpack("<f", string.char(rd8(a), rd8(a+1), rd8(a+2), rd8(a+3)))
        elseif kind == "f64" then return string.unpack("<d", string.char(rd8(a),rd8(a+1),rd8(a+2),rd8(a+3),rd8(a+4),rd8(a+5),rd8(a+6),rd8(a+7)))
        elseif kind == "str" then return "" end
    end,
    read_u32 = rd32, write_u32 = wr32,
    address = function(o) return o end, ptr = function(n) return n end,
    get_main_form = function() return MF end,
    call = function() end, hook = function() return true end, unhook = function() return true end,
    log = function() end, key_down = function() return 0 end,
    enum_methods = function() return {} end, enum_fields = function() return {} end,
    enum_classes = function() return {} end,
    is_object = function() return false end, classname = function() return "" end,
    frame_number = function() return 1 end,
    on_update = function() end, key_press = function() end, sleep = function() end,
    method_addr = function() return 0 end, field_offset = function() return 0 end,
    get_field = function() return 0 end, set_field = function() end,
    get_ptr_field = function() return 0 end, get_string_field = function() return "" end,
    read_string = function() return "" end, read_ptr = function() return 0 end,
    write_u32_ = nil, is_string = function() return false end,
    window_rect = function() return 0, 0, 800, 600 end,
    cursor_pos = function() return 0, 0 end,
    overlay_create = function() return true end, overlay_begin = function() end,
    overlay_rect = function() end, overlay_text = function() end, overlay_commit = function() end,
    add_menu_button = function() return true end, remove_menu_button = function() end,
    console = function() return false end,
}

dofile("mods/00_pivotlib.lua")       -- legacy (sets _G.pivotlib)
dofile("mods/00_pivotlib2.lua")      -- typed layer + merge block

-- legacy side survived
assert(type(pivotlib) == "table")
assert(pivotlib.VERSION == "0.4.0", "legacy VERSION lost")
assert(type(pivotlib.main) == "function", "legacy pivotlib.main lost")
assert(type(pivotlib.method_names) == "function", "legacy pivotlib.method_names lost")
assert(type(pivotlib.hud) == "function", "legacy pivotlib.hud lost")

-- typed side merged in
assert(type(pivotlib.frame_count) == "function", "frame_count not merged")
assert(pivotlib.frame_count() == 1, "merged frame_count broken")
assert(type(pivotlib.frame) == "function", "frame not merged")
assert(type(pivotlib.ui) == "table" and type(pivotlib.ui.button) == "function", "ui not merged")
assert(type(pivotlib.events) == "table", "events not merged")
assert(type(pivotlib.call_addr) == "function", "call_addr not merged")
assert(pivotlib.v2 == pl2, "v2 alias broken")
assert(pivotlib2 == pl2, "pivotlib2 alias broken")

-- legacy name collision policy: legacy wins, no clobber
-- (VERSION exists on legacy; v2's _VERSION must not overwrite it)
assert(pivotlib.VERSION == "0.4.0")

print("MERGED LIBRARY TEST PASSED (legacy + typed APIs on one pivotlib)")
