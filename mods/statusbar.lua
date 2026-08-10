-- statusbar.lua - reads TMainForm's published component fields.
--
-- TMainForm exposes ~146 published fields: the FMX controls on the form
-- (buttons, menus, status bar, dialogs, ...). pivot.get_field returns the
-- object pointer as an integer.

local form = pivot.get_main_form()

local play  = pivot.get_field(form, "PlayButton")
local stop  = pivot.get_field(form, "StopButton")
local status = pivot.get_field(form, "StatusBar")

pivot.log(string.format("PlayButton=0x%X StopButton=0x%X StatusBar=0x%X",
                        play or 0, stop or 0, status or 0))

pivot.log("statusbar.lua done.")
