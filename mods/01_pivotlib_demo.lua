-- 01_pivotlib_demo.lua - showcases the pivotlib abstraction layer.
-- Loaded after 00_pivotlib.lua. Watch the status bar when Pivot runs.

local pivotlib = require("pivotlib")

pivotlib.log("--- pivotlib demo ---")

local form = pivotlib.main()
if not form then
    pivotlib.log("demo: no main form available")
    return
end

pivotlib.log("demo: main form is " .. tostring(form))
pivotlib.log(string.format("demo: %d methods, %d fields (from runtime RTTI)",
                           #form.Methods, #form.Fields))

-- write status / labels so you can see the abstraction working
pivotlib.figure_status("pivotlib demo running")
pivotlib.set_text(form.ZoomLabel, "pivotlib!")

-- every second, show the parsed frame number in the status bar
pivotlib.every(1000, function()
    pivotlib.frame_status(string.format("frame %s of %s",
        tostring(pivotlib.frame()), tostring(pivotlib.num_frames())))
end)

pivotlib.log("demo: ready. Watch the status bar.")
