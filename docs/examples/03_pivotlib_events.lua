-- Demonstrate polled input events and bridge/console commands.

local pivotlib = require("pivotlib")

local clicks = 0
local show = true

pivotlib.on_mouse_down(function(button, x, y, shift)
    clicks = clicks + 1
    pivotlib.log(string.format("events: click #%d at (%.0f, %.0f) button=%d",
                               clicks, x, y, button))
end)

pivotlib.register_command("clicks", function()
    return "click count: " .. clicks
end)

pivotlib.overlay_create()
pivotlib.on_update(function()
    if not show then return end
    pivotlib.overlay_begin()
    pivotlib.overlay_text(10, 8,
        "clicks: " .. clicks .. "  (F2 toggles, 'clicks' via bridge/console)",
        12, 0xFFFFFFFF)
    pivotlib.overlay_commit()
end)

pivotlib.bind("F2", function() show = not show end)

pivotlib.log("events_demo: ready. Click the canvas; run 'clicks' from the console or pivotctl.py.")
