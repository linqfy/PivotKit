-- mock_pivot.lua - simulated pivot.* API for testing pivotlib without pivot.exe
--
-- Objects are integers (addresses). `pivot.ptr` is identity, `is_object` checks
-- the registry. State (fields, methods, call history) lives in `mock.objects`.
--
-- USAGE in tests:
--   local mock = dofile("tests/mock_pivot.lua")
--   _G.pivot = mock
--   ... build objects, publish methods ...
--   local pivotlib = dofile("mods/00_pivotlib.lua")

local M = {}

M.objects = {}
M.classes = {}       -- classname -> { vmt = int, instances = {} }
M.next_addr = 0x100000
M.next_vmt = 0x900000
M.main_form = nil
M.logs = {}
M.update = nil
M.keys = {}          -- vk -> bool (for key_down)
M.cursor = { x = 0, y = 0 }
M.overlay = { begins = 0, items = {} }
M.reload_calls = {}
M.reload_all_calls = 0
M.hooks = {}         -- record of pivotlib.hook/unhook activity
M.hooks_by_method = {} -- [obj][method] -> fn (for fire)
M.unhook_calls = {}
M.mem = {}           -- [addr][off] -> value (for pivot.peek)
M.console_calls = {}
M.console_visible = false

function M.new_object(classname, fields)
    M.next_addr = M.next_addr + 0x100
    local addr = M.next_addr
    local cls = M.classes[classname]
    if not cls then
        M.next_vmt = M.next_vmt + 0x1000
        cls = { vmt = M.next_vmt, instances = {} }
        M.classes[classname] = cls
    end
    cls.instances[#cls.instances + 1] = addr
    M.objects[addr] = {
        class = classname,
        fields = fields or {},
        methods = {},
        history = {},
    }
    return addr
end

function M.publish(addr, methods)
    for k, v in pairs(methods) do M.objects[addr].methods[k] = v end
end

function M.history(addr)
    return M.objects[addr].history
end

-- ------------------------------------------------------------ API surface
function M.ptr(v) return v end            -- identity: mock objects are ints
function M.address(v) return v end

function M.is_object(v)
    return type(v) == "number" and M.objects[v] ~= nil
end

function M.is_string(v)
    return type(v) == "string"
end

function M.class(name)
    local c = M.classes[name]
    return c and c.vmt or nil
end

function M.enum_classes()
    local r = {}
    for n in pairs(M.classes) do r[#r + 1] = n end
    table.sort(r)
    return r
end

function M.find_instance(classType)
    for name, c in pairs(M.classes) do
        if c.vmt == classType and c.instances[1] then
            return c.instances[1]
        end
    end
    return nil
end

function M.find_instances(classType, max)
    max = max or 16
    local r = {}
    for name, c in pairs(M.classes) do
        if c.vmt == classType then
            for i = 1, math.min(max, #c.instances) do r[#r + 1] = c.instances[i] end
        end
    end
    return r
end

function M.get_main_form() return M.main_form end

function M.classname(v)
    local o = M.objects[v]
    return o and o.class or ""
end

function M.enum_methods(v)
    local o = M.objects[v]
    local r = {}
    if o then for n in pairs(o.methods) do r[#r + 1] = n end end
    table.sort(r)
    return r
end

function M.enum_fields(v)
    local o = M.objects[v]
    local r = {}
    if o then for n in pairs(o.fields) do r[#r + 1] = n end end
    table.sort(r)
    return r
end

function M.get_ptr_field(v, name)
    local o = M.objects[v]
    return o and o.fields[name] or nil
end

function M.get_field(v, name)
    return M.get_ptr_field(v, name)
end

function M.set_field(v, name, value)
    local o = M.objects[v]
    if not o then error("mock set_field: no such object") end
    o.fields[name] = value
end

local function record(addr, name, args)
    local o = M.objects[addr]
    if o then o.history[#o.history + 1] = { name = name, args = args } end
end

function M.call(v, name, ...)
    local o = M.objects[v]
    if not o then error("mock call: no such object") end
    local fn = o.methods[name]
    if not fn then error("mock call: method '" .. name .. "' not found on " .. o.class) end
    record(v, name, { ... })
    return fn(...)
end

function M.call_string(v, name, str, ...)
    local o = M.objects[v]
    if not o then error("mock call_string: no such object") end
    local fn = o.methods[name]
    if not fn then error("mock call_string: method '" .. name .. "' not found on " .. o.class) end
    record(v, name, { str, ... })
    return fn(str, ...)
end

function M.call_string_ret(v, name, ...)
    local o = M.objects[v]
    if not o then error("mock call_string_ret: no such object") end
    local fn = o.methods[name]
    if not fn then error("mock call_string_ret: method '" .. name .. "' not found on " .. o.class) end
    record(v, name, { ... })
    local r = fn(...)
    return tostring(r)
end

function M.get_string_field(v, name)
    local o = M.objects[v]
    return o and tostring(o.fields[name]) or nil
end

function M.set_string_field(v, name, str)
    local o = M.objects[v]
    if not o then error("mock set_string_field: no such object") end
    o.fields[name] = tostring(str)
end

function M.get_single_field(v, name)
    local o = M.objects[v]
    return o and tonumber(o.fields[name]) or nil
end

function M.set_single_field(v, name, x)
    local o = M.objects[v]
    if not o then error("mock set_single_field: no such object") end
    o.fields[name] = x
end

function M.get_double_field(v, name) return M.get_single_field(v, name) end
function M.set_double_field(v, name, x) return M.set_single_field(v, name, x) end

function M.get_bool_field(v, name)
    local o = M.objects[v]
    return not not (o and o.fields[name])
end

function M.set_bool_field(v, name, b)
    local o = M.objects[v]
    if not o then error("mock set_bool_field: no such object") end
    o.fields[name] = not not b
end

function M.log(...)
    local parts = {}
    for i = 1, select("#", ...) do parts[i] = tostring(select(i, ...)) end
    M.logs[#M.logs + 1] = table.concat(parts, "\t")
end

function M.on_update(fn) M.update = fn end
function M.frame_number() return 0 end

function M.add_menu_button() return true end
function M.remove_menu_button() return true end
function M.sprite() return 1 end
function M.window_rect() return 0, 0, 800, 600 end
function M.key_press() end
function M.key_down(vk) return M.keys[vk] == true end
function M.sleep() end
function M.cursor_pos() return M.cursor.x, M.cursor.y end

function M.hook(obj, method, fn)
    M.hooks[#M.hooks + 1] = { op = "hook", method = method }
    M.hooks_by_method[obj] = M.hooks_by_method[obj] or {}
    M.hooks_by_method[obj][method] = fn
    return true
end

-- Fire a previously-registered hook as the C stub would: fn(self, args...).
function M.fire(obj, method, ...)
    local h = M.hooks_by_method[obj] and M.hooks_by_method[obj][method]
    if not h then error("mock fire: no hook for " .. tostring(method)) end
    return h(obj, ...)
end

function M.unhook(obj, method)
    M.hooks[#M.hooks + 1] = { op = "unhook", method = method }
    M.unhook_calls[method] = (M.unhook_calls[method] or 0) + 1
    return true
end

function M.reload(modname)
    if modname then
        M.reload_calls[#M.reload_calls + 1] = modname
    else
        M.reload_all_calls = M.reload_all_calls + 1
    end
    return true
end

-- overlay surface (records primitives)
local function ov(prim)
    if prim then M.overlay.items[#M.overlay.items + 1] = prim end
    return true
end
function M.overlay_create() M.overlay.created = true; return true end
function M.overlay_destroy() M.overlay.created = false; return true end
function M.overlay_begin() M.overlay.begins = M.overlay.begins + 1; M.overlay.items = {}; return true end
function M.overlay_text(x, y, s, size, argb) return ov({ t = "text", x = x, y = y, s = s, size = size, argb = argb }) end
function M.overlay_line(x1, y1, x2, y2, argb, width) return ov({ t = "line", x1 = x1, y1 = y1, x2 = x2, y2 = y2, argb = argb, width = width }) end
function M.overlay_rect(x1, y1, x2, y2, argb) return ov({ t = "rect", x1 = x1, y1 = y1, x2 = x2, y2 = y2, argb = argb }) end
function M.overlay_circle(x, y, r, argb) return ov({ t = "circle", x = x, y = y, r = r, argb = argb }) end
function M.overlay_commit() M.overlay.commits = (M.overlay.commits or 0) + 1; return true end

function M.peek(ptr, off, kind)
    local t = M.mem[ptr]
    return t and t[off] or nil
end

function M.console(show)
    M.console_calls[#M.console_calls + 1] = show
    if show ~= nil then M.console_visible = show end
    return M.console_visible == true
end

return M
