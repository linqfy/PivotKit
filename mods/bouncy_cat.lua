-- bouncy_cat.lua - beginner example using the generic sprite API.
-- Click "Spawn Cat" to release the cat; it bounces around the window like
-- the DVD screensaver logo.

pivot.log("bouncy_cat: adding the Spawn Cat button...")

local cat = nil

pivot.add_menu_button("Spawn Cat", function()
    pivot.log("bouncy_cat: meow! releasing the cat.")

    if not cat then
        cat = pivot.sprite("pivotkit/mods/silly_cat.jpg")
    end
    pivot.sprite_show(cat)

    -- Centre it in the window and give it a gentle velocity.
    local l, t, r, b = pivot.window_rect()
    pivot.sprite_move(cat, (l + r) / 2, (t + b) / 2)
    pivot.sprite_velocity(cat, 3, 2)
    pivot.sprite_bounce(cat, true)
end)

pivot.log("bouncy_cat: ready. Click 'Spawn Cat' (top-right of the window).")
