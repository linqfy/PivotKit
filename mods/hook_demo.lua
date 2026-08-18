-- Demonstrate observing and overriding published methods.

local form = pivot.get_main_form()

-- Observe without overriding.
pivot.hook(form, "SetNumFrames", function(self, n)
    pivot.log("[observe] SetNumFrames(" .. tostring(n) .. ")")
    return nil
end)

-- Override the return value.
pivot.hook(form, "GetFrameTween", function(self)
    pivot.log("[override] GetFrameTween -> 9000")
    return 9000
end)

pivot.log("hook_demo: SetNumFrames and GetFrameTween are hooked.")
