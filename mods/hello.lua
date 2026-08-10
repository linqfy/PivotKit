-- hello.lua - the classic smoke test.
-- If this runs, the loader is alive and the TMainForm instance was found.

pivot.log("Hello from a Lua mod!")
local form = pivot.get_main_form()
pivot.log("TMainForm instance: " .. tostring(form))
