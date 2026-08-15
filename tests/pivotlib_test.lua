-- pivotlib_test.lua - validates pivotlib against the mock pivot API.
-- Run: bin\lua.exe tests\pivotlib_test.lua   (from the pivotkit/ root)

local mock = dofile("tests/mock_pivot.lua")
_G.pivot = mock

-- ------------------------------------------------------------ build world
local form = mock.new_object("TMainForm", {
    PlayButton = nil, StopButton = nil,
    FrameStatus = nil, FigureStatus = nil, ZoomLabel = nil,
})
local playBtn      = mock.new_object("TButton", {})
local stopBtn      = mock.new_object("TButton", {})
local frameStatus  = mock.new_object("TLabel", { Text = "Frame 3 of 12" })
local figureStatus = mock.new_object("TLabel", { Text = "" })
local zoomLabel    = mock.new_object("TLabel", { Text = "100%" })

mock.objects[form].fields.PlayButton   = playBtn
mock.objects[form].fields.StopButton   = stopBtn
mock.objects[form].fields.FrameStatus  = frameStatus
mock.objects[form].fields.FigureStatus = figureStatus
mock.objects[form].fields.ZoomLabel    = zoomLabel
mock.main_form = form

mock.publish(form, {
    SetFrameNumber       = function(n) mock.objects[form]._frame = n; return 1 end,
    SetNumFrames         = function(n) mock.objects[form]._numframes = n; return 1 end,
    PlayButtonClick      = function() return 7 end,
    StopButtonClick      = function() return 0 end,
    GetFrameTween        = function() return 42 end,
    SetFrameTween        = function(v) mock.objects[form]._tween = v; return 0 end,
    ZInMenuItemClick     = function() return 0 end,
    ZOutMenuItemClick    = function() return 0 end,
    Undo1Click           = function() return 0 end,
    Redo1Click           = function() return 0 end,
    NextFrameButtonClick = function() return 0 end,
    SelectAll1Click      = function() return 0 end,
    FlipButtonClick      = function() return 0 end,
    CenterButtonClick    = function() return 0 end,
})

mock.publish(frameStatus, {
    SetText = function(t) mock.objects[frameStatus].fields.Text = t end,
    GetText = function() return mock.objects[frameStatus].fields.Text end,
})
mock.publish(figureStatus, {
    SetText = function(t) mock.objects[figureStatus].fields.Text = t end,
})

-- ------------------------------------------------------------ load library
local pivotlib = dofile("mods/00_pivotlib.lua")

local pass, fail = 0, 0
local function check(cond, msg)
    if cond then
        pass = pass + 1
    else
        fail = fail + 1
        print("FAIL: " .. msg)
    end
end

local h = function(addr) return mock.history(addr) end
local last = function(addr) local t = h(addr); return t[#t] end

-- ------------------------------------------------------------ main proxy
local m = pivotlib.main()
check(m ~= nil, "main() returns a proxy")
check(tostring(m):match("TMainForm"), "tostring shows class name (got: " .. tostring(m) .. ")")
check(m.Class == "TMainForm", "proxy .Class")
check(m.Address == form, "proxy .Address")

-- field access: object fields become proxies
local pb = m.PlayButton
check(pb ~= nil, "PlayButton resolves")
check(pb.Class == "TButton", "PlayButton is a TButton proxy")
check(pb.Address == playBtn, "PlayButton points at the mock button")

-- method call, colon style
local r = m:PlayButtonClick()
check(r == 7, "m:PlayButtonClick() returns 7 (got " .. tostring(r) .. ")")
check(last(form).name == "PlayButtonClick", "colon call recorded PlayButtonClick")

-- method call, dot style
m.SelectAll1Click()
check(last(form).name == "SelectAll1Click", "dot call recorded SelectAll1Click")
check(#last(form).args == 0, "dot call passes no self argument")

-- catalog introspection
local names = pivotlib.method_names(form)
check(#names == 14, "method_names lists 14 published methods (got " .. #names .. ")")
check(pivotlib.has_method(form, "SetFrameNumber"), "has_method SetFrameNumber")
check(pivotlib.has_field(form, "PlayButton"), "has_field PlayButton")

-- ------------------------------------------------------------ playback
pivotlib.set_frame(5)
check(mock.objects[form]._frame == 5, "set_frame(5) reaches SetFrameNumber")
pivotlib.set_num_frames(20)
check(mock.objects[form]._numframes == 20, "set_num_frames(20)")
check(pivotlib.tween() == 42, "tween() reads GetFrameTween")
pivotlib.set_tween(10)
check(mock.objects[form]._tween == 10, "set_tween(10)")
pivotlib.play()
check(last(form).name == "PlayButtonClick", "play() clicks PlayButton")
pivotlib.stop()
check(last(form).name == "StopButtonClick", "stop() clicks StopButton")
pivotlib.next_frame()
check(last(form).name == "NextFrameButtonClick", "next_frame()")

-- frame() / num_frames() parsed from status text
check(pivotlib.frame() == 3, "frame() parses 'Frame 3 of 12' -> 3 (got " .. tostring(pivotlib.frame()) .. ")")
check(pivotlib.num_frames() == 12, "num_frames() -> 12 (got " .. tostring(pivotlib.num_frames()) .. ")")

-- ------------------------------------------------------------ figures
pivotlib.select_all()
check(last(form).name == "SelectAll1Click", "select_all()")
pivotlib.flip()
check(last(form).name == "FlipButtonClick", "flip()")
pivotlib.center()
check(last(form).name == "CenterButtonClick", "center()")

-- ------------------------------------------------------------ text
pivotlib.set_text(figureStatus, "hello")
check(pivotlib.get_text(figureStatus) == "hello", "set_text/get_text roundtrip")
pivotlib.figure_status("selected 2")
check(mock.objects[figureStatus].fields.Text == "selected 2", "figure_status() sets text")
check(pivotlib.get_string(figureStatus, "Text") == "selected 2", "get_string reads Text")

-- ------------------------------------------------------------ events
local a, b = 0, 0
pivotlib.on_update(function(fr) a = fr end)
pivotlib.on_update(function(fr) b = fr * 2 end)

local n = 0
pivotlib.every(1000, function() n = n + 1 end)

for i = 1, 60 do mock.update(i) end
check(a == 60 and b == 120, "on_update dispatch to multiple subscribers (a=" .. a .. ", b=" .. b .. ")")
check(n == 1, "every(1000) fires after ~60 frames (n=" .. n .. ")")
for i = 61, 120 do mock.update(i) end
check(n == 2, "every(1000) fires again (n=" .. n .. ")")

-- ------------------------------------------------------------ scene / scan
local figA = mock.new_object("TFigure", { Name = "hero", X = 10, Y = 20 })
local figB = mock.new_object("TFigure", { Name = "enemy", X = -5, Y = 7 })
local figs = pivotlib.figures()
check(#figs == 2, "figures() finds 2 TFigure instances (got " .. #figs .. ")")
check(figs[1].Class == "TFigure" or figs[2].Class == "TFigure", "figures() returns TFigure proxies")

local scanned = pivotlib.scan("TFigure")
check(#scanned == 2, "scan('TFigure') finds 2 (got " .. #scanned .. ")")

local inst, fields = pivotlib.probe("TFigure")
check(inst ~= nil, "probe('TFigure') returns an instance")
check(fields.Name == "hero" or fields.Name == "enemy",
      "probe reads string field Name (got " .. tostring(fields.Name) .. ")")
check(fields.X == 10 or fields.X == -5, "probe reads number field X")

check(pivotlib.read_field(figA, "Name") == "hero", "read_field detects string field")
check(pivotlib.read_field(figA, "X") == 10, "read_field detects number field")

-- ------------------------------------------------------------ hooks & mods
pivotlib.hook(form, "SetFrameNumber", function(self, n) return nil end)
pivotlib.hook(form, "GetFrameTween", function(self) return nil end)
check(#mock.hooks == 2, "pivotlib.hook calls pivot.hook twice")
pivotlib.unhook_all()
check(mock.unhook_calls["SetFrameNumber"] == 1, "unhook_all unhooked SetFrameNumber")
check(mock.unhook_calls["GetFrameTween"] == 1, "unhook_all unhooked GetFrameTween")

pivotlib.register_command("spin", function() return 42 end)
check(select(2, pivotlib.run_command("spin")) == 42, "run_command('spin') -> 42")
check(pivotlib.commands()[1] == "spin", "commands() lists registered commands")
local okc, errc = pivotlib.run_command("nope")
check(okc == nil and errc, "run_command of unknown command fails")

pivotlib.reload("hook_demo")
check(mock.reload_calls[#mock.reload_calls] == "hook_demo", "reload('hook_demo') calls pivot.reload")
pivotlib.reload()
check(mock.reload_all_calls == 1, "reload() triggers full reload")

-- ------------------------------------------------------------ keybindings
local pressed = 0
pivotlib.bind("P", function() pressed = pressed + 1 end)
pivotlib.bind("F5", function() pressed = pressed + 10 end)
for i = 1, 5 do mock.update(i) end
check(pressed == 0, "bind does not fire while key is up")
mock.keys[0x50] = true
mock.update(6)
check(pressed == 1, "bind('P') fires on press edge")
mock.update(7)           -- still held: no repeat
check(pressed == 1, "bind does not auto-repeat")
mock.keys[0x50] = false
mock.update(8)
mock.keys[0x50] = true
mock.update(9)
check(pressed == 2, "bind fires again on next press edge")
mock.keys[0x50] = false
mock.keys[0x74] = true   -- F5
mock.update(10)
check(pressed == 12, "bind('F5') uses named VK (pressed=" .. pressed .. ")")
mock.keys[0x74] = false

-- ------------------------------------------------------------ overlay
pivotlib.overlay_create()
pivotlib.overlay_begin()
pivotlib.overlay_text(10, 20, "hello hud", 14, 0xFFFF0000)
pivotlib.overlay_line(0, 0, 100, 100, 0xFF00FF00, 2)
pivotlib.overlay_rect(5, 5, 50, 50, 0xFF0000FF)
pivotlib.overlay_circle(30, 30, 8, 0xFFFFFF00)
pivotlib.overlay_commit()
check(mock.overlay.begins == 1, "overlay_begin recorded")
check(#mock.overlay.items == 4, "overlay primitives recorded (got " .. #mock.overlay.items .. ")")
check(mock.overlay.items[1].t == "text" and mock.overlay.items[1].s == "hello hud",
      "overlay_text recorded with string")
check(mock.overlay.items[2].t == "line", "overlay_line recorded")
check(mock.overlay.commits == 1, "overlay_commit recorded")

-- hud() convenience: draws every frame
local hudCalls = 0
pivotlib.hud(function() hudCalls = hudCalls + 1 end)
mock.update(11)
check(hudCalls == 1, "hud() runs its draw fn each frame")
check(mock.overlay.begins >= 2, "hud() calls overlay_begin each frame")

-- ------------------------------------------------------------ result
print(string.format("\nRESULT: %d passed, %d failed", pass, fail))
if fail > 0 then os.exit(1) end
