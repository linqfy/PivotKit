-- 00_pivotlib.lua - high-level abstraction layer over pivot.*
--
-- Loaded first (00_ prefix, alphabetical) and registered in package.preload,
-- so any mod can use `local pivotlib = require("pivotlib")` (or just the
-- global `pivotlib`) to get this same singleton.
--
-- What it adds on top of the raw pivot module:
--   * proxy objects  - form and controls become Lua objects; published fields
--                      are read lazily (object fields become proxies too) and
--                      published methods are called naturally:
--                        local f = pivotlib.main()
--                        f:SetFrameNumber(3)
--                        local play = f.PlayButton          -- a proxy
--   * full catalog   - every published method/field is reachable through the
--                      proxy metatable, no hand-written wrappers needed
--   * typed access   - string / float / double / bool fields via the new
--                      pivot.* accessors, with graceful fallbacks
--   * semantic API   - play/stop/frame nav, figure ops, zoom/camera, status
--                      bar text, file & undo, events
--   * events         - pivotlib.on_update(fn) (multi-subscriber) and
--                      pivotlib.every(ms, fn) on top of pivot's single tick

local pivotlib = {}

pivotlib.VERSION = "0.1.0"

-- ---------------------------------------------------------------------------
-- internal state
-- ---------------------------------------------------------------------------

local proxy_cache   = {}   -- addr(int) -> proxy
local class_catalog = {}   -- classname -> { methods = set, fields = set }
local update_handlers = {} -- list of fn(frame)
local timer_handlers  = {} -- list of { ticks, left, fn }
local frame_track     = nil
local numframes_track = nil
local dispatcher_on   = false
local started         = false

-- ---------------------------------------------------------------------------
-- low-level helpers (addresses are always integers internally; converted to
-- lightuserdata via pivot.ptr only when calling the C API)
-- ---------------------------------------------------------------------------

local function to_int(v)
    if type(v) == "userdata" then return pivot.address(v) end
    if type(v) == "number"   then return math.floor(v) end
    if type(v) == "table"    then return rawget(v, "__addr") end
    return nil
end

local function ptr_of(v)
    local i = to_int(v)
    return i and pivot.ptr(i) or nil
end

local function proxy_addr(v)
    if type(v) == "table" then return rawget(v, "__addr") end
    return nil
end

local function safe_call(ok, ...) return ok, ... end

local function wrap_call(obj, name, ...)
    local p = ptr_of(obj)
    if not p then error("pivotlib: nil object in call to " .. name) end
    local r = pivot.call(p, name, ...)
    if r and pivot.is_object and pivot.is_object(r) then
        return pivotlib.obj(r)
    end
    return r
end

-- ---------------------------------------------------------------------------
-- catalog / introspection (cached per class name)
-- ---------------------------------------------------------------------------

local function obj_classname(v)
    local p = ptr_of(v)
    if not p then return nil end
    local ok, cn = pcall(pivot.classname, p)
    if not ok or cn == "" then return nil end
    return cn
end

function pivotlib.catalog(obj)
    local cn = obj_classname(obj)
    if not cn then return nil end
    if not class_catalog[cn] then
        local methods, fields = {}, {}
        local p = ptr_of(obj)
        local ok, ms = pcall(pivot.enum_methods, p)
        if ok and ms then for _, n in ipairs(ms) do methods[n] = true end end
        local ok2, fs = pcall(pivot.enum_fields, p)
        if ok2 and fs then for _, n in ipairs(fs) do fields[n] = true end end
        class_catalog[cn] = { methods = methods, fields = fields }
    end
    return class_catalog[cn]
end

function pivotlib.methods(obj)
    local c = pivotlib.catalog(obj)
    return c and c.methods or {}
end

function pivotlib.fields(obj)
    local c = pivotlib.catalog(obj)
    return c and c.fields or {}
end

function pivotlib.method_names(obj)
    local m = {}
    for n in pairs(pivotlib.methods(obj)) do m[#m + 1] = n end
    table.sort(m)
    return m
end

function pivotlib.field_names(obj)
    local f = {}
    for n in pairs(pivotlib.fields(obj)) do f[#f + 1] = n end
    table.sort(f)
    return f
end

function pivotlib.has_method(obj, name)
    return pivotlib.methods(obj)[name] == true
end

function pivotlib.has_field(obj, name)
    return pivotlib.fields(obj)[name] == true
end

-- ---------------------------------------------------------------------------
-- proxies
-- ---------------------------------------------------------------------------

local proxy_mt = {}

local function p_self_class(self)  return rawget(self, "__class") end
local function p_self_addr(self)   return rawget(self, "__addr") end
local function p_self_cache(self)  return rawget(self, "__cache") end

function proxy_mt.__index(self, key)
    if key == "Class"   then return p_self_class(self) end
    if key == "Address" then return p_self_addr(self) end
    if key == "Methods" then return pivotlib.method_names(self) end
    if key == "Fields"  then return pivotlib.field_names(self) end

    local cache = p_self_cache(self)
    local cached = cache and cache[key]
    if cached ~= nil then return cached end

    if pivotlib.has_method(self, key) then
        local fn = function(...)
            -- `obj:Method(...)` passes the proxy as the first argument; drop
            -- it so the Delphi method only sees real args (`obj.Method(...)`
            -- works too, unless the first real arg is the object itself).
            if select("#", ...) > 0 and (...) == self then
                return wrap_call(self, key, select(2, ...))
            end
            return wrap_call(self, key, ...)
        end
        cache[key] = fn
        return fn
    end

    if pivotlib.has_field(self, key) then
        local p = ptr_of(self)
        local ok, raw = pcall(pivot.get_ptr_field, p, key)
        local v = nil
        if ok and raw ~= nil then
            if pivot.is_object and pivot.is_object(raw) then
                v = pivotlib.obj(raw)
            else
                v = to_int(raw)
            end
        end
        cache[key] = v
        return v
    end

    return nil
end

function proxy_mt.__newindex(self, key, value)
    if pivotlib.has_field(self, key) then
        local i = to_int(value)
        if i == nil then i = value end
        pivot.set_field(ptr_of(self), key, i)
        local cache = p_self_cache(self)
        if cache then cache[key] = nil end
        return
    end
    rawset(self, key, value)
end

function proxy_mt.__tostring(self)
    return string.format("%s@0x%X", p_self_class(self) or "?", p_self_addr(self) or 0)
end

function pivotlib.obj(v)
    if type(v) == "table" and rawget(v, "__addr") then return v end
    local i = to_int(v)
    if not i then return nil end
    local cached = proxy_cache[i]
    if cached then return cached end
    if not (pivot.is_object and pivot.is_object(i)) then return nil end
    local cn = obj_classname(i)
    local self = setmetatable({}, proxy_mt)
    rawset(self, "__addr", i)
    rawset(self, "__class", cn or "?")
    rawset(self, "__cache", {})
    proxy_cache[i] = self
    return self
end

function pivotlib.main()
    local f = pivot.get_main_form()
    return pivotlib.obj(f)
end

-- ---------------------------------------------------------------------------
-- typed access & calls
-- ---------------------------------------------------------------------------

local function wrap_result(r)
    if r and pivot.is_object and pivot.is_object(r) then
        return pivotlib.obj(r)
    end
    return r
end

function pivotlib.call_method(obj, name, ...)
    local p = ptr_of(obj)
    if not p then error("pivotlib.call_method: nil object") end
    return wrap_result(pivot.call(p, name, ...))
end

function pivotlib.call_method_string(obj, name, str, ...)
    local p = ptr_of(obj)
    if not p then error("pivotlib.call_method_string: nil object") end
    return pivot.call_string(p, name, tostring(str), ...)
end

function pivotlib.call_method_string_ret(obj, name, ...)
    local p = ptr_of(obj)
    if not p then error("pivotlib.call_method_string_ret: nil object") end
    return pivot.call_string_ret(p, name, ...)
end

function pivotlib.get_string(obj, name)
    local p = ptr_of(obj)
    if not p then return nil end
    local ok, v = pcall(pivot.get_string_field, p, name)
    return ok and v or nil
end

function pivotlib.set_string(obj, name, str)
    local p = ptr_of(obj)
    if not p then return false end
    local ok = pcall(pivot.set_string_field, p, name, tostring(str))
    return ok == true
end

function pivotlib.get_number(obj, name, kind)
    kind = kind or "single"
    local p = ptr_of(obj)
    if not p then return nil end
    local getter = pivot["get_" .. kind .. "_field"]
    if not getter then return nil end
    local ok, v = pcall(getter, p, name)
    return ok and v or nil
end

function pivotlib.set_number(obj, name, value, kind)
    kind = kind or "single"
    local p = ptr_of(obj)
    if not p then return false end
    local setter = pivot["set_" .. kind .. "_field"]
    if not setter then return false end
    return pcall(setter, p, name, value)
end

function pivotlib.get_bool(obj, name)
    local p = ptr_of(obj)
    if not p then return nil end
    local ok, v = pcall(pivot.get_bool_field, p, name)
    return ok and v or nil
end

function pivotlib.set_bool(obj, name, value)
    local p = ptr_of(obj)
    if not p then return false end
    return pcall(pivot.set_bool_field, p, name, value)
end

-- Text convenience with graceful fallbacks (SetText/GetText methods first,
-- then published "Text" field).
function pivotlib.set_text(control, text)
    local p = ptr_of(control)
    if not p then return false end
    local ok = pcall(pivot.call_string, p, "SetText", tostring(text))
    if ok then return true end
    local ok2 = pcall(pivot.set_string_field, p, "Text", tostring(text))
    return ok2 == true
end

function pivotlib.get_text(control)
    local p = ptr_of(control)
    if not p then return nil end
    local ok, s = pcall(pivot.call_string_ret, p, "GetText")
    if ok and s and s ~= "" then return s end
    local ok2, s2 = pcall(pivot.get_string_field, p, "Text")
    return ok2 and s2 or nil
end

-- ---------------------------------------------------------------------------
-- playback & frames
-- ---------------------------------------------------------------------------

local function main()
    local m = pivotlib.main()
    if not m then
        pivot.log("pivotlib: main form not available")
        return nil
    end
    return m
end

function pivotlib.play()  local m = main(); return m and m:PlayButtonClick() or false end
function pivotlib.stop()  local m = main(); return m and m:StopButtonClick() or false end
function pivotlib.next_frame()  local m = main(); return m and m:NextFrameButtonClick() or false end

function pivotlib.set_frame(n)
    local m = main(); if not m then return false end
    return pcall(function() m:SetFrameNumber(n) end)
end

function pivotlib.set_num_frames(n)
    local m = main(); if not m then return false end
    return pcall(function() m:SetNumFrames(n) end)
end

local function frame_from_status()
    local m = pivotlib.main()
    if not m then return nil, nil end
    local lbl = m.FrameStatus
    if not lbl then return nil, nil end
    local s = pivotlib.get_text(lbl)
    if not s then return nil, nil end
    local cur, tot = s:match("(%d+)%s*[o/]f?%s*(%d+)")
    if not cur then cur = s:match("(%d+)") end
    return cur and tonumber(cur) or nil, tot and tonumber(tot) or nil
end

function pivotlib.frame()
    if frame_track then return frame_track end
    return frame_from_status()
end

function pivotlib.num_frames()
    if numframes_track then return numframes_track end
    return select(2, frame_from_status())
end

function pivotlib.tween()
    local m = main(); return m and m:GetFrameTween() or nil
end

function pivotlib.set_tween(v)
    local m = main(); if not m then return false end
    return pcall(function() m:SetFrameTween(v) end)
end

-- optional: keep frame tracking accurate by hooking SetFrameNumber. Not
-- enabled by default (hooks are a shared resource); call track_frames(true).
function pivotlib.track_frames(enabled)
    local m = main(); if not m then return false end
    local key = "SetFrameNumber"
    local p = ptr_of(m)
    if enabled then
        return pcall(pivot.hook, p, key, function(self, n)
            frame_track = n
            return nil
        end)
    else
        return pcall(pivot.unhook, p, key)
    end
end

-- ---------------------------------------------------------------------------
-- figures & selection
-- ---------------------------------------------------------------------------

function pivotlib.select_all()   local m = main(); return m and m:SelectAll1Click() or false end
function pivotlib.duplicate()    local m = main(); return m and m:DuplicateFigures1Click() or false end
function pivotlib.flip()         local m = main(); return m and m:FlipButtonClick() or false end
function pivotlib.center()       local m = main(); return m and m:CenterButtonClick() or false end
function pivotlib.add_figure()   local m = main(); return m and m:AddFigureButtonClick() or false end
function pivotlib.delete_figure() local m = main(); return m and m:DeleteFigureButtonClick() or false end
function pivotlib.join()         local m = main(); return m and m:JoinButtonClick() or false end
function pivotlib.copy_figure()  local m = main(); return m and m:CopyFigureButtonClick() or false end
function pivotlib.paste_figure() local m = main(); return m and m:PasteFigureButtonClick() or false end
function pivotlib.edit_type()    local m = main(); return m and m:EditTypeButtonClick() or false end
function pivotlib.reset_pose()   local m = main(); return m and m:ResetPoseMenuItemClick() or false end

-- ---------------------------------------------------------------------------
-- canvas / zoom / camera
-- ---------------------------------------------------------------------------

function pivotlib.zoom_in()        local m = main(); return m and m:ZInMenuItemClick() or false end
function pivotlib.zoom_out()       local m = main(); return m and m:ZOutMenuItemClick() or false end
function pivotlib.zoom_50()        local m = main(); return m and m:Z50MenuItemClick() or false end
function pivotlib.zoom_100()       local m = main(); return m and m:Z100MenuItemClick() or false end
function pivotlib.zoom_200()       local m = main(); return m and m:Z200MenuItemClick() or false end
function pivotlib.zoom_selected()  local m = main(); return m and m:ZSelMenuItemClick() or false end
function pivotlib.zoom_anim()      local m = main(); return m and m:ZAnimMenuItemClick() or false end

function pivotlib.show_cam()       local m = main(); return m and m:ShowCamMenuItemClick() or false end
function pivotlib.reset_cam()      local m = main(); return m and m:ResetCamMenuItemClick() or false end
function pivotlib.copy_cam()       local m = main(); return m and m:CopyCamMenuItemClick() or false end
function pivotlib.paste_cam()      local m = main(); return m and m:PasteCamMenuItemClick() or false end
function pivotlib.align_cam()      local m = main(); return m and m:AlignCamMenuItemClick() or false end
function pivotlib.apply_cam()      local m = main(); return m and m:ApplyCamMenuItemClick() or false end
function pivotlib.center_cam_figs() local m = main(); return m and m:CenterCamFigsMenuItemClick() or false end
function pivotlib.zoom_cam()       local m = main(); return m and m:ZCamMenuItemClick() or false end

-- ---------------------------------------------------------------------------
-- status bar & UI
-- ---------------------------------------------------------------------------

local function status_label(name)
    local m = main()
    if not m then return nil end
    return m[name]
end

function pivotlib.figure_status(t)  local l = status_label("FigureStatus");  return l and pivotlib.set_text(l, t) or false end
function pivotlib.frame_status(t)   local l = status_label("FrameStatus");   return l and pivotlib.set_text(l, t) or false end
function pivotlib.segment_status(t) local l = status_label("SegmentStatus"); return l and pivotlib.set_text(l, t) or false end
function pivotlib.zoom_label(t)     local l = status_label("ZoomLabel");     return l and pivotlib.set_text(l, t) or false end
function pivotlib.frame_label(t)    local l = status_label("FrameLabel");    return l and pivotlib.set_text(l, t) or false end
function pivotlib.tween_label(t)    local l = status_label("TweenLabel");    return l and pivotlib.set_text(l, t) or false end

-- ---------------------------------------------------------------------------
-- file & undo
-- ---------------------------------------------------------------------------

function pivotlib.undo() local m = main(); return m and m:Undo1Click() or false end
function pivotlib.redo() local m = main(); return m and m:Redo1Click() or false end
function pivotlib.new_animation()  local m = main(); return m and m:New1Click() or false end
function pivotlib.save()      local m = main(); return m and m:Save1Click() or false end
function pivotlib.save_as()   local m = main(); return m and m:SaveAs1Click() or false end
function pivotlib.open_dialog() local m = main(); return m and m:OpenAnimation1Click() or false end
function pivotlib.export()    local m = main(); return m and m:Export1Click() or false end
function pivotlib.load_background() local m = main(); return m and m:LoadBackground1Click() or false end
function pivotlib.load_sprite() local m = main(); return m and m:LoadSprite1Click() or false end
function pivotlib.load_figure() local m = main(); return m and m:LoadFigure1Click() or false end

-- programmatic open/save (needs the string-aware call bridge). These are
-- best-effort: signatures aren't documented, so failures are reported.
function pivotlib.load_project(path)
    local m = main(); if not m then return false end
    local ok, err = pcall(pivot.call_string, ptr_of(m), "LoadProject", tostring(path))
    if not ok then pivot.log("pivotlib: load_project failed: " .. tostring(err)) end
    return ok
end

-- Programmatic SVG export (best-effort: signatures not documented).
function pivotlib.export_svg(path)
    local m = main(); if not m then return false, "no form" end
    local ok, err = pcall(pivot.call_string, ptr_of(m), "SaveSVG", tostring(path))
    if not ok then
        ok, err = pcall(pivot.call_string, ptr_of(m), "SVG1Click", tostring(path))
    end
    return ok, err
end

function pivotlib.export_svg_nohandles(path)
    local m = main(); if not m then return false, "no form" end
    return pcall(pivot.call_string, ptr_of(m), "SVGnohandles1Click", tostring(path))
end

-- ---------------------------------------------------------------------------
-- keybindings (polled in the per-frame dispatcher; keys are virtual-key codes)
-- ---------------------------------------------------------------------------

local binds = {}   -- { vk, fn, down }

local function vk_of(key)
    if type(key) == "number" then return key end
    key = tostring(key):upper()
    local named = {
        ENTER=13, ESC=27, SPACE=32, TAB=9,
        F1=112, F2=113, F3=114, F4=115, F5=116, F6=117,
        F7=118, F8=119, F9=120, F10=121, F11=122, F12=123,
        UP=38, DOWN=40, LEFT=37, RIGHT=39,
        HOME=36, END=35, PGUP=33, PGDN=34, INSERT=45, DELETE=46,
    }
    if named[key] then return named[key] end
    local c = key:byte(1, 1)
    return c  -- ASCII == VK for letters/digits
end

-- Bind a key to a function. `key` is a VK code or a name ("P", "F5", ...).
-- Fires on the press edge (rising edge).
function pivotlib.bind(key, fn)
    local vk = vk_of(key)
    assert(vk, "pivotlib.bind: unknown key " .. tostring(key))
    assert(type(fn) == "function", "pivotlib.bind: fn must be a function")
    binds[#binds + 1] = { vk = vk, fn = fn, down = false }
end

function pivotlib.unbind_all()
    for i = #binds, 1, -1 do binds[i] = nil end
end

-- ---------------------------------------------------------------------------
-- hooks & mod manager
-- ---------------------------------------------------------------------------

local hook_registry = {}   -- { obj = addr, method = name }

-- Same as pivot.hook but registered so pivotlib.reload / unhook_all can undo
-- every hook cleanly before re-running mods.
function pivotlib.hook(obj, method, fn)
    local p = ptr_of(obj)
    if not p then return false end
    local ok = pcall(pivot.hook, p, method, fn)
    if ok then
        hook_registry[#hook_registry + 1] = {
            obj = proxy_addr(obj) or to_int(obj), method = method,
        }
    end
    return ok
end

function pivotlib.unhook(obj, method)
    local p = ptr_of(obj)
    if not p then return false end
    local ok = pcall(pivot.unhook, p, method)
    if ok then
        local a = proxy_addr(obj) or to_int(obj)
        for i = #hook_registry, 1, -1 do
            if hook_registry[i].method == method and hook_registry[i].obj == a then
                table.remove(hook_registry, i)
            end
        end
    end
    return ok
end

function pivotlib.unhook_all()
    for i = #hook_registry, 1, -1 do
        local h = hook_registry[i]
        pcall(pivot.unhook, pivot.ptr(h.obj), h.method)
        hook_registry[i] = nil
    end
end

-- Command registry (a tiny in-mod console).
local commands = {}

function pivotlib.register_command(name, fn)
    assert(type(name) == "string", "pivotlib.register_command: name")
    assert(type(fn) == "function", "pivotlib.register_command: fn")
    commands[name] = fn
end

function pivotlib.commands()
    local out = {}
    for n in pairs(commands) do out[#out + 1] = n end
    table.sort(out)
    return out
end

function pivotlib.run_command(name, ...)
    local fn = commands[name]
    if not fn then return nil, "no such command: " .. tostring(name) end
    return pcall(fn, ...)
end

-- Hot reload. With a mod name ("foo" -> foo.lua) only that mod re-runs; with
-- no argument every mod re-runs. All registered hooks are removed first so a
-- reload never double-hooks. Frame/timer handlers are reset on a full reload.
function pivotlib.reload(modname)
    pivotlib.unhook_all()
    if modname then
        return pcall(pivot.reload, tostring(modname))
    end
    for i = #update_handlers, 1, -1 do update_handlers[i] = nil end
    for i = #timer_handlers, 1, -1 do timer_handlers[i] = nil end
    return pcall(pivot.reload)
end

-- ---------------------------------------------------------------------------
-- scene / introspection (runtime instance discovery)
-- ---------------------------------------------------------------------------

function pivotlib.class(name) return pivot.class(name) end
function pivotlib.classname(v) return pivot.classname(ptr_of(v) or v) end

-- Return proxies for live instances of a class (by name, classType, or a
-- sample object).
function pivotlib.scan(classname_or_obj, max)
    local ct
    if type(classname_or_obj) == "string" then
        ct = pivot.class(classname_or_obj)
    elseif proxy_addr(classname_or_obj) then
        ct = pivot.class(pivot.classname(classname_or_obj))
    elseif pivot.is_object and pivot.is_object(classname_or_obj) then
        ct = pivot.class(pivot.classname(pivot.ptr(to_int(classname_or_obj))))
    else
        ct = to_int(classname_or_obj)
    end
    if not ct then return {} end
    local out = {}
    for _, inst in ipairs(pivot.find_instances(ct, max or 32)) do
        local p = pivotlib.obj(inst)
        if p then out[#out + 1] = p end
    end
    return out
end

-- Class names in the image that mention "Figure" (figure/frame classes).
function pivotlib.figure_classes()
    local out = {}
    for _, name in ipairs(pivot.enum_classes() or {}) do
        if name:find("Figure") then out[#out + 1] = name end
    end
    table.sort(out)
    return out
end

-- Best-effort live figure instances (whatever figure/frame classes exist).
function pivotlib.figures()
    local out = {}
    for _, cls in ipairs(pivotlib.figure_classes()) do
        for _, inst in ipairs(pivotlib.scan(cls)) do
            out[#out + 1] = inst
        end
    end
    return out
end

-- Read a field with type detection: object -> proxy, Delphi string -> string,
-- otherwise the raw 32-bit value.
function pivotlib.read_field(obj, name)
    local p = ptr_of(obj)
    if not p then return nil end
    local raw = pivot.get_ptr_field(p, name)
    if raw ~= nil then
        if pivot.is_object and pivot.is_object(raw) then return pivotlib.obj(raw) end
        if pivot.is_string and pivot.is_string(raw) then
            return pivot.get_string_field(p, name)
        end
        return to_int(raw)
    end
    return nil
end

-- Probe the first live instance of a class: returns (proxy, {field=value,...}).
function pivotlib.probe(classname)
    local list = pivotlib.scan(classname, 1)
    if #list == 0 then return nil, nil end
    local inst = list[1]
    local fields = {}
    for _, name in ipairs(pivotlib.field_names(inst)) do
        fields[name] = pivotlib.read_field(inst, name)
    end
    return inst, fields
end

-- ---------------------------------------------------------------------------
-- events
-- ---------------------------------------------------------------------------

local function install_dispatcher()
    if dispatcher_on then return end
    dispatcher_on = true
    pivot.on_update(function(frame)
        local i = 1
        while i <= #update_handlers do
            local ok, err = pcall(update_handlers[i], frame)
            if not ok then
                pivot.log("pivotlib: on_update error: " .. tostring(err))
                table.remove(update_handlers, i)
            else
                i = i + 1
            end
        end
        for j = #timer_handlers, 1, -1 do
            local t = timer_handlers[j]
            t.left = t.left - 1
            if t.left <= 0 then
                t.left = t.ticks
                local ok, err = pcall(t.fn)
                if not ok then
                    pivot.log("pivotlib: timer error: " .. tostring(err))
                    table.remove(timer_handlers, j)
                end
            end
        end
        for _, b in ipairs(binds) do
            local down = pivot.key_down(b.vk)
            if down and not b.down then
                b.down = true
                local ok, err = pcall(b.fn)
                if not ok then pivot.log("pivotlib: bind error: " .. tostring(err)) end
            elseif not down then
                b.down = false
            end
        end
    end)
end

-- Register a per-frame callback (multiple subscribers allowed).
function pivotlib.on_update(fn)
    assert(type(fn) == "function", "on_update expects a function")
    update_handlers[#update_handlers + 1] = fn
    install_dispatcher()
end

-- Run fn roughly every `ms` milliseconds (frame-count based, ~60fps).
function pivotlib.every(ms, fn)
    assert(type(fn) == "function", "every expects a function")
    local ticks = math.max(1, math.floor((ms or 1000) / (1000 / 60)))
    timer_handlers[#timer_handlers + 1] = { ticks = ticks, left = ticks, fn = fn }
    install_dispatcher()
end

function pivotlib.menu_button(label, fn)
    return pivot.add_menu_button(label, fn)
end

function pivotlib.remove_menu_button()
    return pivot.remove_menu_button()
end

-- ---------------------------------------------------------------------------
-- misc passthroughs
-- ---------------------------------------------------------------------------

function pivotlib.log(...) return pivot.log(...) end
function pivotlib.sleep(ms) return pivot.sleep(ms) end
function pivotlib.key_press(vk) return pivot.key_press(vk) end
function pivotlib.key_down(vk) return pivot.key_down(vk) end
function pivotlib.window_rect() return pivot.window_rect() end
function pivotlib.sprite(path) return pivot.sprite(path) end
function pivotlib.sprite_move(h, x, y) return pivot.sprite_move(h, x, y) end
function pivotlib.sprite_velocity(h, vx, vy) return pivot.sprite_velocity(h, vx, vy) end
function pivotlib.sprite_bounce(h, b) return pivot.sprite_bounce(h, b) end
function pivotlib.sprite_show(h) return pivot.sprite_show(h) end
function pivotlib.sprite_hide(h) return pivot.sprite_hide(h) end
function pivotlib.sprite_destroy(h) return pivot.sprite_destroy(h) end

-- Canvas overlay (HUD) wrappers.
function pivotlib.overlay_create()  return pivot.overlay_create() end
function pivotlib.overlay_destroy() return pivot.overlay_destroy() end
function pivotlib.overlay_begin()   return pivot.overlay_begin() end
function pivotlib.overlay_text(x, y, s, size, argb)
    return pivot.overlay_text(x, y, s, size, argb)
end
function pivotlib.overlay_line(x1, y1, x2, y2, argb, width)
    return pivot.overlay_line(x1, y1, x2, y2, argb, width)
end
function pivotlib.overlay_rect(x1, y1, x2, y2, argb)
    return pivot.overlay_rect(x1, y1, x2, y2, argb)
end
function pivotlib.overlay_circle(x, y, r, argb)
    return pivot.overlay_circle(x, y, r, argb)
end
function pivotlib.overlay_commit()  return pivot.overlay_commit() end

-- Convenience: draw `fn` into the canvas overlay every frame.
function pivotlib.hud(fn)
    assert(type(fn) == "function", "pivotlib.hud: fn must be a function")
    pivotlib.overlay_create()
    pivotlib.on_update(function()
        pivotlib.overlay_begin()
        local ok, err = pcall(fn)
        if not ok then pivotlib.log("pivotlib: hud error: " .. tostring(err)) end
        pivotlib.overlay_commit()
    end)
end

-- ---------------------------------------------------------------------------
-- startup
-- ---------------------------------------------------------------------------

function pivotlib.start()
    if started then return pivotlib end
    started = true
    local form = pivotlib.main()
    if form then
        pivot.log(string.format("pivotlib %s ready: %s", pivotlib.VERSION, tostring(form)))
        local m = pivotlib.method_names(form)
        local f = pivotlib.field_names(form)
        pivot.log(string.format("pivotlib: catalog %d methods, %d fields (via runtime RTTI)",
                                #m, #f))
    else
        pivot.log("pivotlib: main form not found at load time (will retry lazily)")
    end
    install_dispatcher()
    return pivotlib
end

package.preload["pivotlib"] = function() return pivotlib end

if pivot and pivot.get_main_form then
    local ok, err = pcall(pivotlib.start)
    if not ok then pivot.log("pivotlib: start error: " .. tostring(err)) end
end

_G.pivotlib = pivotlib
return pivotlib
