-- Minimal loader smoke test.

pivot.log("Hello from a Lua mod!")
local form = pivot.get_main_form()
pivot.log("TMainForm instance: " .. tostring(form))
