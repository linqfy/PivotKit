-- Read TMainForm's published component fields.

local form = pivot.get_main_form()

local play  = pivot.get_field(form, "PlayButton")
local stop  = pivot.get_field(form, "StopButton")
local status = pivot.get_field(form, "StatusBar")

pivot.log(string.format("PlayButton=0x%X StopButton=0x%X StatusBar=0x%X",
                        play or 0, stop or 0, status or 0))

pivot.log("statusbar.lua done.")
