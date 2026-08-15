-- 02_pivotlib_hud.lua - demo of the canvas overlay, keybindings and commands.
--
-- Press F1 (or run the "hud" command) to toggle an on-screen HUD that shows
-- the current frame and a crosshair in the middle of the Pivot window.

local pivotlib = require("pivotlib")

local hud_on = false

local function draw_hud()
    if not hud_on then return end
    local l, t, r, b = pivotlib.window_rect()
    local w, h = r - l, b - t

    pivotlib.overlay_begin()

    -- frame info, top-left
    pivotlib.overlay_text(10, 8,
        "PivotKit HUD - frame " .. tostring(pivotlib.frame())
        .. " / " .. tostring(pivotlib.num_frames()),
        13, 0xFFFFFFFF)

    -- crosshair at window center
    pivotlib.overlay_line(w / 2 - 20, h / 2, w / 2 + 20, h / 2, 0x80FFFFFF, 1)
    pivotlib.overlay_line(w / 2, h / 2 - 20, w / 2, h / 2 + 20, 0x80FFFFFF, 1)
    pivotlib.overlay_circle(w / 2, h / 2, 40, 0x40FF00FF)

    pivotlib.overlay_commit()
end

local function toggle_hud()
    hud_on = not hud_on
    pivotlib.log("hud_demo: HUD " .. (hud_on and "on" or "off"))
end

pivotlib.overlay_create()
pivotlib.register_command("hud", toggle_hud)
pivotlib.bind("F1", toggle_hud)
pivotlib.on_update(draw_hud)

pivotlib.log("hud_demo: ready. Press F1 or run the 'hud' command to toggle the overlay.")
