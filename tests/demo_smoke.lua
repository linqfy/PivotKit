-- Smoke-test the shipped mods with the mock API.

local mock = dofile("tests/mock_pivot.lua")
_G.pivot = mock

local form = mock.new_object("TMainForm", {
    FrameStatus = nil, FigureStatus = nil, ZoomLabel = nil,
})
local frameStatus  = mock.new_object("TLabel", { Text = "Frame 1 of 10" })
local figureStatus = mock.new_object("TLabel", { Text = "" })
local zoomLabel    = mock.new_object("TLabel", { Text = "100%" })

mock.objects[form].fields.FrameStatus  = frameStatus
mock.objects[form].fields.FigureStatus = figureStatus
mock.objects[form].fields.ZoomLabel    = zoomLabel
mock.main_form = form

for _, l in ipairs({ frameStatus, figureStatus, zoomLabel }) do
    mock.publish(l, {
        SetText = function(t) mock.objects[l].fields.Text = t end,
        GetText = function() return mock.objects[l].fields.Text end,
    })
end

local pivotlib = dofile("mods/00_pivotlib.lua")
dofile("mods/01_pivotlib_demo.lua")
dofile("mods/02_pivotlib_hud.lua")
dofile("mods/03_pivotlib_events.lua")

-- Toggle the HUD through the normal polled key path.
mock.keys[0x70] = true      -- F1
mock.update(1)
mock.keys[0x70] = false
mock.update(2)
print("HUD toggle draws overlay prims: " .. tostring(#mock.overlay.items > 0))

-- Simulate one polled left-click in window-relative coordinates.
mock.cursor.x, mock.cursor.y = 40, 25
mock.keys[0x01] = true
mock.update(3)
mock.keys[0x01] = false
mock.update(4)
assert(pivotlib.console_exec("clicks") == "click count: 1")

for i = 5, 65 do mock.update(i) end

print("--- pivotkit.log ---")
for _, line in ipairs(mock.logs) do print(line) end
print("--- status texts ---")
print("FrameStatus = " .. tostring(mock.objects[frameStatus].fields.Text))
print("FigureStatus = " .. tostring(mock.objects[figureStatus].fields.Text))
print("ZoomLabel = " .. tostring(mock.objects[zoomLabel].fields.Text))
