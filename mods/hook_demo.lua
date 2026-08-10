-- hook_demo.lua - shows how to observe and override methods.
--
-- pivot.hook(obj, "MethodName", fn) intercepts the published method.
--   * return nil      -> the original method still runs (observe)
--   * return a number -> that value is returned instead (override)

local form = pivot.get_main_form()

-- Observe: log every time the app changes the frame count.
pivot.hook(form, "SetNumFrames", function(self, n)
    pivot.log("[observe] SetNumFrames(" .. tostring(n) .. ")")
    return nil
end)

-- Override: force a constant return value.
pivot.hook(form, "GetFrameTween", function(self)
    pivot.log("[override] GetFrameTween -> 9000")
    return 9000
end)

pivot.log("hook_demo: SetNumFrames and GetFrameTween are hooked.")
